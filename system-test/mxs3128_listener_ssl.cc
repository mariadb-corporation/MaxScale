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

    auto conn = test.maxscales->rwsplit();
    conn.ssl(false);
    test.expect(conn.connect(), "Connection without SSL should work: %s", conn.error());
    test.expect(conn.query("select 1"), "Query should work: %s", conn.error());

    std::string home = test.maxscales->access_homedir();
    std::string ssl_key = home + "/certs/server-key.pem";
    std::string ssl_cert = home + "/certs/server-cert.pem";
    std::string ssl_ca = home + "/certs/ca.pem";
    test.check_maxctrl("alter listener RW-Split-Listener "
                       "ssl true ssl_key " + ssl_key + " ssl_cert " + ssl_cert + " ssl_ca_cert " + ssl_ca);

    test.expect(!conn.connect(), "Connection without SSL should fail");

    conn.ssl(true);
    test.expect(conn.connect(), "Connection with SSL should work: %s", conn.error());
    test.expect(conn.query("select 1"), "Query should work: %s", conn.error());

    test.check_maxctrl("alter listener RW-Split-Listener ssl false");

    // TODO: SSL connections will be created but they won't use TLS. Figure out if there's
    // a way to tell Connector-C to reject non-TLS connections.
    test.expect(conn.connect(), "Connection with SSL should work: %s", conn.error());
    test.expect(conn.query("select 1"), "Query should work: %s", conn.error());

    return test.global_result;
}
