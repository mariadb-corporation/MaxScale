/**
 * Test routing with services as targets for other services
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto conn1 = test.maxscale->rwsplit();
    test.expect(conn1.connect(), "Connection should work: %s", conn1.error());
    test.expect(conn1.query("SELECT @@server_id"), "Query should work: %s", conn1.error());

    auto conn2 = test.maxscale->rwsplit();
    test.expect(conn2.connect(), "Connection should work: %s", conn2.error());
    test.expect(conn2.query("SELECT @@server_id"), "Query should work: %s", conn2.error());

    test.repl->block_all_nodes();
    test.maxscale->wait_for_monitor();

    test.expect(!conn1.query("SELECT @@server_id"), "First query should fail");
    test.expect(!conn2.query("SELECT @@server_id"), "Second query should fail");

    test.repl->unblock_all_nodes();
    test.maxscale->wait_for_monitor();

    test.expect(conn1.connect(), "Connection should work: %s", conn1.error());
    test.expect(conn1.query("SELECT @@server_id"), "Query should work: %s", conn1.error());

    test.expect(conn2.connect(), "Connection should work: %s", conn2.error());
    test.expect(conn2.query("SELECT @@server_id"), "Query should work: %s", conn2.error());

    return test.global_result;
}
