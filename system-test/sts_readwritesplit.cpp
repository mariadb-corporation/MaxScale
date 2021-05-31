/**
 * Test routing with services as targets for other services
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.repl->connect();
    auto ids = test.repl->get_all_server_ids_str();
    test.repl->disconnect();

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection should work: %s", conn.error());


    test.log_printf("Test 1: Routing sanity check");

    auto server_id = conn.field("SELECT @@server_id");
    test.expect(server_id == ids[3],
                "Select should be routed to server4 used only by service2: %s != %s",
                server_id.c_str(), ids[3].c_str());
    test.expect(conn.field("SELECT @@server_id, @@last_insert_id") == ids[0],
                "Master read should be routed to the master of service1");


    if (test.global_result)
    {
        exit(1);
    }
    test.log_printf("Test 2: Outage of secondary sub-service");

    test.repl->block_node(3);
    test.maxscale->wait_for_monitor();

    server_id = conn.field("SELECT @@server_id");
    test.expect(server_id == ids[1] || server_id == ids[2],
                "Select should be routed to server2 or server3 used by service1");
    test.expect(conn.field("SELECT @@server_id, @@last_insert_id") == ids[0],
                "Master read should be routed to the master of service1");


    if (test.global_result)
    {
        exit(1);
    }
    test.log_printf("Test 3: Total sub-service outage");

    test.repl->block_node(0);
    test.maxscale->wait_for_monitor();

    test.expect(!conn.query("SELECT @@last_insert_id"), "Master read should fail");

    test.repl->unblock_node(0);
    test.repl->unblock_node(3);
    test.maxscale->wait_for_monitor();
    test.expect(conn.connect(), "Reconnection should work: %s", conn.error());


    if (test.global_result)
    {
        exit(1);
    }
    test.log_printf("Test 4: Backend failure mid-query");

    std::thread([&test]() {
                    sleep(3);
                    test.repl->block_node(3);
                }).detach();

    server_id = conn.field("SELECT @@server_id, SLEEP(10)");
    test.expect(!server_id.empty() && server_id != ids[3],
                "Read should be replayed when sub-service fails");

    // The readwritesplit on the upper level will try to reroute the failed read to the other service
    test.expect(conn.query("SELECT 1"), "Subsequent read after failure should work: %s", conn.error());

    // Reconnecting will use only the first service as the second service has no running servers
    test.expect(conn.connect(), "Reconnection should work: %s", conn.error());
    test.expect(conn.query("SELECT 1"), "Read after reconnection should work: %s", conn.error());

    // Unblock and reconnect so that both services are in use
    test.repl->unblock_node(3);
    test.maxscale->wait_for_monitor();
    test.expect(conn.connect(), "Reconnection should work: %s", conn.error());


    if (test.global_result)
    {
        exit(1);
    }
    test.log_printf("Test 5: Master failure mid-query");

    std::thread([&test]() {
                    sleep(3);
                    test.repl->block_node(0);
                }).detach();

    test.expect(!conn.query("SELECT @@last_insert_id, SLEEP(10)"),
                "Master read should fail when sub-service fails");
    test.expect(!conn.query("SELECT 1"), "Subsequent read after failure should fail");

    test.repl->unblock_node(0);
    test.maxscale->wait_for_monitor();

    test.expect(conn.connect(), "Reconnection should work: %s", conn.error());
    test.expect(conn.query("SELECT 1"), "Read after reconnection should work: %s", conn.error());


    return test.global_result;
}
