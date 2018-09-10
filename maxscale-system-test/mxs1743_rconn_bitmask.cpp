/**
 * MXS-1743: Maxscale unable to enforce round-robin between read service for Slave
 *
 * https://jira.mariadb.org/browse/MXS-1743
 */
#include "testconnections.h"
#include <vector>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto do_test = [&]() {
            test.set_timeout(20);
            test.maxscales->connect();
            test.try_query(test.maxscales->conn_master[0], "SELECT 1");
            test.maxscales->disconnect();
            test.stop_timeout();
        };

    test.tprintf("Testing with both master and slave up");
    do_test();

    test.tprintf("Testing with only the master");
    test.repl->block_node(0);
    test.maxscales->wait_for_monitor();
    do_test();
    test.repl->unblock_node(0);
    test.maxscales->wait_for_monitor();

    test.tprintf("Testing with only the slave");
    test.repl->block_node(1);
    test.maxscales->wait_for_monitor();
    do_test();
    test.repl->unblock_node(1);
    test.maxscales->wait_for_monitor();

    test.tprintf("Checking that both the master and slave are used");
    std::vector<MYSQL*> connections;

    test.repl->connect();
    execute_query_silent(test.repl->nodes[0], "DROP USER IF EXISTS 'mxs1743'@'%'");
    test.try_query(test.repl->nodes[0], "%s", "CREATE USER 'mxs1743'@'%' IDENTIFIED BY 'mxs1743'");
    test.try_query(test.repl->nodes[0], "%s", "GRANT ALL ON *.* TO 'mxs1743'@'%'");
    test.repl->sync_slaves();

    for (int i = 0; i < 20; i++)
    {
        // Open a connection and make sure it works
        test.set_timeout(20);
        MYSQL* conn = open_conn(test.maxscales->readconn_master_port[0],
                                test.maxscales->IP[0],
                                "mxs1743",
                                "mxs1743",
                                false);
        test.try_query(conn, "SELECT 1");
        connections.push_back(conn);
        test.stop_timeout();
    }

    // Give the connections a few seconds to establish
    sleep(5);

    std::string query
        = "SELECT COUNT(*) AS connections FROM information_schema.processlist WHERE user = 'mxs1743'";
    char master_connections[1024];
    char slave_connections[1024];
    find_field(test.repl->nodes[0], query.c_str(), "connections", master_connections);
    find_field(test.repl->nodes[1], query.c_str(), "connections", slave_connections);

    test.expect(strcmp(master_connections, slave_connections) == 0,
                "Master and slave shoud have the same amount of connections: %s != %s",
                master_connections,
                slave_connections);

    for (auto a : connections)
    {
        mysql_close(a);
    }

    execute_query_silent(test.repl->nodes[0], "DROP USER 'mxs1743'@'%'");
    test.repl->disconnect();

    return test.global_result;
}
