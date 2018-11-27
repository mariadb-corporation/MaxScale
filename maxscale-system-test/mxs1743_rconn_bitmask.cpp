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

    auto do_test = [&]()
    {
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

    test.tprintf("Opening new connections to verify readconnroute works");

    for (int i = 0; i < 20; i++)
    {
        test.set_timeout(20);
        MYSQL* conn = test.maxscales->open_readconn_master_connection();
        test.try_query(conn, "SELECT 1");
        connections.push_back(conn);
        test.stop_timeout();
    }

    int rc;
    char* s1 = test.maxscales->ssh_node_output(0, "maxctrl --tsv list servers|grep server1|cut -f 4", true, &rc);
    char* s2 = test.maxscales->ssh_node_output(0, "maxctrl --tsv list servers|grep server2|cut -f 4", true, &rc);


    test.expect(strcmp(s1, s2) == 0,
                "Master and slave shoud have the same amount of connections: %s != %s",
                s1, s2);

    free(s1);
    free(s2);

    for (auto a : connections)
    {
        mysql_close(a);
    }

    return test.global_result;
}
