/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-3128: Listener alteration
 *
 * Checks that listener SSL can be enabled and disabled at runtime.
 */

#include <maxtest/testconnections.hh>
#include <iostream>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    auto& mxs = *test.maxscale;

    auto conn = mxs.rwsplit();
    conn.ssl(false);
    test.expect(conn.connect(), "Connection without SSL should work: %s", conn.error());
    test.expect(conn.query("select 1"), "Query should work: %s", conn.error());

    std::string ssl_key = mxs.cert_key_path();
    std::string ssl_cert = mxs.cert_path();
    std::string ssl_ca = mxs.ca_cert_path();
    test.check_maxctrl("alter listener RW-Split-Listener "
                       "ssl true ssl_key " + ssl_key + " ssl_cert " + ssl_cert + " ssl_ca_cert " + ssl_ca);

    test.expect(!conn.connect(), "Connection without SSL should fail");

    conn.ssl(true);
    test.expect(conn.connect(), "Connection with SSL should work: %s", conn.error());
    test.expect(conn.query("select 1"), "Query should work: %s", conn.error());

    test.check_maxctrl("alter listener RW-Split-Listener ssl=false");

    // TODO: SSL connections will be created but they won't use TLS. Figure out if there's
    // a way to tell Connector-C to reject non-TLS connections.
    test.expect(conn.connect(), "Connection with SSL should work: %s", conn.error());
    test.expect(conn.query("select 1"), "Query should work: %s", conn.error());

    return test.global_result;
}
