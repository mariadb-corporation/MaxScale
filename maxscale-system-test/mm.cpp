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

int check_conf(TestConnections* Test, int blocked_node)
{
    int global_result = 0;
    Test->set_timeout(60);

    Test->repl->connect();
    Test->connect_rwsplit();
    create_t1(Test->maxscales->conn_rwsplit[0]);
    global_result += insert_into_t1(Test->maxscales->conn_rwsplit[0], 4);

    printf("Sleeping to let replication happen\n");
    fflush(stdout);
    Test->stop_timeout();
    sleep(30);

    for (int i = 0; i < 2; i++)
    {
        if ( i != blocked_node)
        {
            Test->tprintf("Checking data from node %d (%s)\n", i, Test->repl->IP[i]);
            Test->set_timeout(100);
            global_result += select_from_t1(Test->repl->nodes[i], 4);
        }
    }
    Test->set_timeout(100);
    printf("Checking data from rwsplit\n");
    fflush(stdout);
    global_result += select_from_t1(Test->maxscales->conn_rwsplit[0], 4);
    global_result += execute_query(Test->maxscales->conn_rwsplit[0], "DROP TABLE t1");

    Test->repl->close_connections();
    mysql_close(Test->maxscales->conn_rwsplit[0]);

    Test->stop_timeout();
    return global_result;
}

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(120);
    char maxadmin_result[1024];

    Test->repl->set_repl_user();

    Test->start_mm(); // first node - slave, second - master

    Test->set_timeout(120);
    Test->get_maxadmin_param((char *) "show server server1", (char *) "Status:", maxadmin_result);
    Test->tprintf("node0 %s\n", maxadmin_result);
    if (strstr(maxadmin_result, "Slave, Running")  == NULL )
    {
        Test->add_result(1, "Node0 is not slave, status is %s\n", maxadmin_result);
    }
    Test->set_timeout(120);
    Test->get_maxadmin_param((char *) "show server server2", (char *) "Status:", maxadmin_result);
    Test->tprintf("node1 %s\n", maxadmin_result);
    if (strstr(maxadmin_result, "Master, Running") == NULL )
    {
        Test->add_result(1, "Node1 is not master, status is %s\n", maxadmin_result);
    }
    Test->set_timeout(120);
    printf("Put some data and check\n");
    Test->add_result(check_conf(Test, 2), "Configuration broken\n");
    Test->set_timeout(120);
    Test->tprintf("Block slave\n");
    Test->repl->block_node(0);
    Test->stop_timeout();
    sleep(15);
    Test->set_timeout(120);
    Test->get_maxadmin_param((char *) "show server server1", (char *) "Status:", maxadmin_result);
    printf("node0 %s\n", maxadmin_result);
    if (strstr(maxadmin_result, "Down")  == NULL )
    {
        Test->add_result(1, "Node0 is not down, status is %s\n", maxadmin_result);
    }
    Test->set_timeout(120);
    Test->tprintf("Put some data and check\n");
    Test->add_result(check_conf(Test, 0), "configuration broken\n");

    Test->set_timeout(120);
    Test->tprintf("Unlock slave\n");
    Test->repl->unblock_node(0);
    sleep(15);

    Test->set_timeout(120);
    Test->tprintf("Block master\n");
    Test->repl->block_node(1);
    sleep(15);
    Test->get_maxadmin_param((char *) "show server server2", (char *) "Status:", maxadmin_result);
    printf("node1 %s\n", maxadmin_result);
    if (strstr(maxadmin_result, "Down")  == NULL )
    {
        Test->add_result(1, "Node1 is not down, status is %s\n", maxadmin_result);
    }
    Test->tprintf("Make node 1 master\n");

    Test->set_timeout(120);
    Test->repl->connect();
    execute_query(Test->repl->nodes[0], (char *) "SET GLOBAL READ_ONLY=OFF");
    Test->repl->close_connections();

    sleep(15);
    Test->set_timeout(120);
    Test->tprintf("Put some data and check\n");
    Test->add_result(check_conf(Test, 1), "configuration broken\n");

    printf("Unlock slave\n");
    Test->repl->unblock_node(1);
    sleep(15);

    Test->set_timeout(120);
    printf("Make node 2 slave\n");
    Test->repl->connect();
    execute_query(Test->repl->nodes[1], (char *) "SET GLOBAL READ_ONLY=ON");
    Test->repl->close_connections();
    sleep(15);

    Test->set_timeout(120);
    printf("Put some data and check\n");
    Test->add_result(check_conf(Test, 2), "Configuration broken\n");

    Test->set_timeout(60);
    Test->get_maxadmin_param((char *) "show server server2", (char *) "Status:", maxadmin_result);
    printf("node1 %s\n", maxadmin_result);
    if (strstr(maxadmin_result, "Slave, Running")  == NULL )
    {
        Test->add_result(1, "Node1 is not slave, status is %s\n", maxadmin_result);
    }
    Test->set_timeout(60);
    Test->get_maxadmin_param((char *) "show server server1", (char *) "Status:", maxadmin_result);
    Test->tprintf("node0 %s\n", maxadmin_result);
    if (strstr(maxadmin_result, "Master, Running")  == NULL )
    {
        Test->add_result(1, "Node0 is not master, status is %s\n", maxadmin_result);
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}
