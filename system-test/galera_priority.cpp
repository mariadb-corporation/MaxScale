/**
 * @file galera_priority.cpp Galera node priority test
 *
 * Node priorities are configured in the following order:
 * node3 > node1 > node4 > node2
 *
 * The test executes a SELECT @@server_id to get the server id of each
 * node. The same query is executed in a transaction through MaxScale
 * and the server id should match the expected output depending on which
 * of the nodes are available. The simple test blocks nodes from highest priority
 * to lowest priority.
 */

#include <maxtest/testconnections.hh>
#include <maxtest/galera_cluster.hh>

void check_server_id(TestConnections& test, const std::string& id)
{
    test.tprintf("Expecting '%s'...", id.c_str());
    auto conn = test.maxscales->rwsplit();
    test.expect(conn.connect(), "Connection should work: %s", conn.error());
    test.expect(conn.query("BEGIN"), "BEGIN should work: %s", conn.error());
    auto f = conn.field("SELECT @@server_id");
    test.expect(f == id, "Expected server_id '%s', not server_id '%s'", id.c_str(), f.c_str());
    test.expect(conn.query("COMMIT"), "BEGIN should work: %s", conn.error());
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.galera->connect();
    auto ids = test.galera->get_all_server_ids_str();

    /** Node 3 should be master */
    check_server_id(test, ids[2]);

    /** Block node 3 and node 1 should be master */
    test.galera->block_node(2);
    test.maxscales->wait_for_monitor(2);
    check_server_id(test, ids[0]);

    /** Block node 1 and node 4 should be master */
    test.galera->block_node(0);
    test.maxscales->wait_for_monitor(2);
    check_server_id(test, ids[3]);

    /** Block node 4 and node 2 should be master */
    test.galera->block_node(3);
    test.maxscales->wait_for_monitor(2);
    check_server_id(test, ids[1]);

    /** All nodes blocked, expect failure */
    test.galera->block_node(1);
    test.maxscales->wait_for_monitor(2);

    auto conn = test.maxscales->rwsplit();
    test.expect(!conn.connect(), "Connecting to rwsplit should fail");

    /** Unblock all nodes, node 3 should be master again */
    test.galera->unblock_all_nodes();
    test.maxscales->wait_for_monitor(2);
    check_server_id(test, ids[2]);

    /** Restart MaxScale check that states are the same */
    test.maxscales->restart();
    test.maxscales->wait_for_monitor(2);
    check_server_id(test, ids[2]);

    return test.global_result;
}
