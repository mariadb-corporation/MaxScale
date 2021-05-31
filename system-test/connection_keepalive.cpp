/**
 * Test for connection_keepalive
 *
 * The connection should be kept alive even if the session is idle for longer
 * than wait_timeout.
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto conn = test.maxscale->get_connection(4006);
    test.expect(conn.connect(), "Connection should work: %s", conn.error());

    test.expect(conn.query("CREATE OR REPLACE TABLE test.t1(id INT)"), "CREATE should work: %s",
                conn.error());
    test.expect(conn.query("INSERT INTO test.t1 VALUES (1)"), "INSERT should work: %s", conn.error());
    test.expect(conn.query("SELECT 1"), "SELECT should work: %s", conn.error());

    test.tprintf("Configure the connection to time out if it's inactive for 10 seconds");
    test.expect(conn.query("SET wait_timeout=10"), "SET should work: %s", conn.error());

    sleep(20);

    test.expect(conn.query("INSERT INTO test.t1 VALUES (1)"), "INSERT should work: %s", conn.error());
    test.expect(conn.query("SELECT 1"), "SELECT should work: %s", conn.error());

    test.tprintf("Alter the connection_keepalive so that if it takes effect the session will be closed");
    test.check_maxctrl("alter service RW-Split-Router connection_keepalive 3000");

    sleep(20);

    test.tprintf("Make sure the connection uses the new configuration values");
    test.expect(!conn.query("INSERT INTO test.t1 VALUES (1)"), "INSERT should fail");
    test.expect(!conn.query("SELECT 1"), "SELECT should fail");

    conn.disconnect();
    conn.connect();

    test.tprintf("Set wait_timeout again to the same value. The connection should die after 10 seconds.");
    test.expect(conn.query("SET wait_timeout=10"), "SELECT should work: %s", conn.error());

    sleep(20);

    test.expect(!conn.query("INSERT INTO test.t1 VALUES (1)"), "INSERT should fail");
    test.expect(!conn.query("SELECT 1"), "SELECT should fail");


    test.tprintf("Open a connection to a readwritesplit that is using another readwritesplit");
    auto conn2 = test.maxscale->get_connection(4008);
    test.expect(conn2.connect(), "Connection should work: %s", conn2.error());

    test.tprintf("Check that connection keepalive works on the upper level as well");
    test.expect(conn2.query("SET wait_timeout=10"), "SET should work: %s", conn2.error());

    sleep(20);

    test.expect(conn2.query("INSERT INTO test.t1 VALUES (1)"), "INSERT should work: %s", conn2.error());
    test.expect(conn2.query("SELECT 1"), "SELECT should work: %s", conn2.error());

    // Cleanup
    conn.connect();
    conn.query("DROP TABLE test.t1");

    return test.global_result;
}
