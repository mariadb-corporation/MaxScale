/**
 * @file mm test of multi master monitor
 *
 * - us 'mmmon' module as a monitor
 * - reset master, stop slaves, stop all nodes
 * - start 2 nodes
 * - execute SET MASTER TO on node0 to point to node1 and on node1 to point to node0
 * - execute SET GLOBAL READ_ONLY=ON on node0
 * - check server status using maxadmin interface, expect Master on node1 and Slave on node0
 * - put data to DB using RWSplit, check data using RWSplit and directrly from backend nodes
 * - block node0 (slave)
 * - check server status using maxadmin interface, expect node0 Down
 * - put data and check it
 * - unblock node0
 * - block node1 (master)
 * - check server status using maxadmin interface, expect node1 Down
 * - execute SET GLOBAL READ_ONLY=OFF on node0
 * - unblock node0
 * - execute SET GLOBAL READ_ONLY=ON on node1
 * - check server status using maxadmin interface, expect Master on node0 and Slave on node1
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

int check_conf(TestConnections& test, int blocked_node)
{
    int global_result = 0;
    test.set_timeout(60);

    test.repl->connect();
    test.maxscales->connect_rwsplit(0);
    create_t1(test.maxscales->conn_rwsplit[0]);
    global_result += insert_into_t1(test.maxscales->conn_rwsplit[0], 1);

    printf("Sleeping to let replication happen\n");
    fflush(stdout);
    test.stop_timeout();
    sleep(10);

    for (int i = 0; i < 2; i++)
    {
        if (i != blocked_node)
        {
            test.tprintf("Checking data from node %d (%s)\n", i, test.repl->IP[i]);
            test.set_timeout(100);
            global_result += select_from_t1(test.repl->nodes[i], 1);
        }
    }
    test.set_timeout(100);
    printf("Checking data from rwsplit\n");
    fflush(stdout);
    global_result += select_from_t1(test.maxscales->conn_rwsplit[0], 1);
    global_result += execute_query(test.maxscales->conn_rwsplit[0], "DROP TABLE t1");

    test.repl->close_connections();
    mysql_close(test.maxscales->conn_rwsplit[0]);

    test.stop_timeout();
    return global_result;
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.set_timeout(120);
    char maxadmin_result[1024];

    test.repl->set_repl_user();

    test.start_mm(0);   // first node - slave, second - master

    test.set_timeout(120);
    test.maxscales->get_maxadmin_param(0, (char*) "show server server1", (char*) "Status:", maxadmin_result);
    test.tprintf("node0 %s\n", maxadmin_result);
    if (strstr(maxadmin_result, "Slave, Running") == NULL)
    {
        test.add_result(1, "Node0 is not slave, status is %s\n", maxadmin_result);
    }
    test.set_timeout(120);
    test.maxscales->get_maxadmin_param(0, (char*) "show server server2", (char*) "Status:", maxadmin_result);
    test.tprintf("node1 %s\n", maxadmin_result);
    if (strstr(maxadmin_result, "Master, Running") == NULL)
    {
        test.add_result(1, "Node1 is not master, status is %s\n", maxadmin_result);
    }
    test.set_timeout(120);
    printf("Put some data and check\n");
    test.add_result(check_conf(test, 2), "Configuration broken\n");
    test.set_timeout(120);
    test.tprintf("Block slave\n");
    test.repl->block_node(0);
    test.stop_timeout();
    test.maxscales->wait_for_monitor();
    test.set_timeout(120);
    test.maxscales->get_maxadmin_param(0, (char*) "show server server1", (char*) "Status:", maxadmin_result);
    printf("node0 %s\n", maxadmin_result);
    if (strstr(maxadmin_result, "Down") == NULL)
    {
        test.add_result(1, "Node0 is not down, status is %s\n", maxadmin_result);
    }
    test.set_timeout(120);
    test.tprintf("Put some data and check\n");
    test.add_result(check_conf(test, 0), "configuration broken\n");

    test.set_timeout(120);
    test.tprintf("Unlock slave\n");
    test.repl->unblock_node(0);
    test.maxscales->wait_for_monitor();

    test.set_timeout(120);
    test.tprintf("Block master\n");
    test.repl->block_node(1);
    test.maxscales->wait_for_monitor();
    test.maxscales->get_maxadmin_param(0, (char*) "show server server2", (char*) "Status:", maxadmin_result);
    printf("node1 %s\n", maxadmin_result);
    if (strstr(maxadmin_result, "Down") == NULL)
    {
        test.add_result(1, "Node1 is not down, status is %s\n", maxadmin_result);
    }
    test.tprintf("Make node 1 master\n");

    test.set_timeout(120);
    test.repl->connect();
    execute_query(test.repl->nodes[0], (char*) "SET GLOBAL READ_ONLY=OFF");
    test.repl->close_connections();

    test.maxscales->wait_for_monitor();
    test.set_timeout(120);
    test.tprintf("Put some data and check\n");
    test.add_result(check_conf(test, 1), "configuration broken\n");

    printf("Unlock slave\n");
    test.repl->unblock_node(1);
    test.maxscales->wait_for_monitor();

    test.set_timeout(120);
    printf("Make node 2 slave\n");
    test.repl->connect();
    execute_query(test.repl->nodes[1], (char*) "SET GLOBAL READ_ONLY=ON");
    test.repl->close_connections();
    test.maxscales->wait_for_monitor();

    test.set_timeout(120);
    printf("Put some data and check\n");
    test.add_result(check_conf(test, 2), "Configuration broken\n");

    test.set_timeout(60);
    test.maxscales->get_maxadmin_param(0, (char*) "show server server2", (char*) "Status:", maxadmin_result);
    printf("node1 %s\n", maxadmin_result);
    if (strstr(maxadmin_result, "Slave, Running") == NULL)
    {
        test.add_result(1, "Node1 is not slave, status is %s\n", maxadmin_result);
    }
    test.set_timeout(60);
    test.maxscales->get_maxadmin_param(0, (char*) "show server server1", (char*) "Status:", maxadmin_result);
    test.tprintf("node0 %s\n", maxadmin_result);
    if (strstr(maxadmin_result, "Master, Running") == NULL)
    {
        test.add_result(1, "Node0 is not master, status is %s\n", maxadmin_result);
    }

    return test.global_result;
}
