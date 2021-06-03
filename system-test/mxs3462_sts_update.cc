/**
 * MXS-3462: Service-to-service routing doesn't work with the `cluster` parameter.
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection should work: %s", conn.error());
    test.expect(conn.query("SELECT 1"), "SELECT should work: %s", conn.error());
    test.expect(conn.query("SET @a = 1"), "SET should work: %s", conn.error());

    return test.global_result;
}
