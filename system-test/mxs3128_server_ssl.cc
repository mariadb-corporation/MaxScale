/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-3128: Server SSL alteration
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    const char* query = "SELECT variable_value FROM information_schema.session_status "
                        "WHERE variable_name = 'ssl_version'";
    std::string ssl_ca = std::string(test.maxscale->access_homedir()) + "/certs/ca.pem";

    auto c = test.repl->get_connection(0);
    c.connect();
    test.expect(c.query("ALTER USER skysql REQUIRE NONE"), "ALTER USER: %s", c.error());
    test.expect(c.query("ALTER USER maxskysql REQUIRE NONE"), "ALTER USER: %s", c.error());

    for (int i = 1; i <= 4; i++)
    {
        test.check_maxctrl("alter server server" + std::to_string(i) + " ssl false");
    }

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection without SSL should work: %s", conn.error());
    test.expect(conn.field(query).empty(), "SSL should be disabled");

    for (int i = 1; i <= 4; i++)
    {
        test.check_maxctrl("alter server server" + std::to_string(i) + " ssl true ssl_ca_cert " + ssl_ca);
    }

    test.expect(conn.connect(), "Connection with SSL should work: %s", conn.error());
    test.expect(!conn.field(query).empty(), "SSL should be enabled");

    for (int i = 1; i <= 4; i++)
    {
        test.check_maxctrl("alter server server" + std::to_string(i) + " ssl_version TLSv12");
    }

    test.expect(conn.connect(), "Connection with SSL should work: %s", conn.error());
    auto res = conn.field(query);
    test.expect(res == "TLSv1.2", "TLSv1.2 should be in use: %s", res.c_str());

    // Only TLSv1.2 ciphers are configurable in MaxScale. TLSv1.3 uses a different API and should have a new
    // parameter for it.
    for (std::string cipher : {"AES128-SHA256", "AES256-SHA256", "AES128-GCM-SHA256"})
    {
        for (int i = 1; i <= 4; i++)
        {
            test.check_maxctrl("alter server server" + std::to_string(i) + " ssl_cipher " + cipher);
        }

        test.expect(conn.connect(), "Connection with SSL should work: %s", conn.error());
        res = conn.field("SELECT variable_value FROM information_schema.session_status "
                         "WHERE variable_name = 'ssl_cipher'");
        test.expect(res == cipher, "Cipher should be '%s' but is '%s'", cipher.c_str(), res.c_str());
    }

    auto result = test.maxctrl("alter server server1 ssl_version=TLSv13").output;

    if (result.find("TLSv1.3 is not supported") == std::string::npos)
    {
        for (int i = 1; i <= 4; i++)
        {
            test.check_maxctrl("alter server server" + std::to_string(i) + " ssl_version TLSv13");
        }

        auto rv = test.repl->ssh_output("openssl version");

        // OpenSSL 1.0.2 only supports TLSv1.2 and a TLSv1.3 connection should fail.
        if (rv.output.find("1.0.2") != std::string::npos)
        {
            test.expect(!conn.connect(),
                        "Connection with TLSv1.3 should not work if the backend does not support it");
        }
        else
        {
            test.expect(conn.connect(), "Connection with SSL should work: %s", conn.error());
            res = conn.field(query);
            test.expect(res == "TLSv1.3", "TLSv1.3 should be in use: %s", res.c_str());
        }
    }

    for (int i = 1; i <= 4; i++)
    {
        test.check_maxctrl("alter server server" + std::to_string(i) + " ssl false");
    }

    test.expect(conn.connect(), "Connection without SSL should work: %s", conn.error());
    test.expect(conn.field(query).empty(), "SSL should be disabled");

    return test.global_result;
}
