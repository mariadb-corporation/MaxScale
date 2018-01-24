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


#include <iostream>
#include "testconnections.h"

using namespace std;

int check_server_id(TestConnections* test, char *node_id)
{
    char str[1024];
    int rval = 0;
    if (execute_query(test->maxscales->conn_rwsplit[0], "BEGIN") ||
            find_field(test->maxscales->conn_rwsplit[0], "SELECT @@server_id", "@@server_id", str) ||
            execute_query(test->maxscales->conn_rwsplit[0], "COMMIT"))
    {
        test->tprintf("Failed to compare @@server_id.\n");
        rval = 1;
    }
    else if (strcmp(node_id, str))
    {
        test->tprintf("@@server_id is %s instead of %s\n", str, node_id);
        rval = 1;
    }
    return rval;
}

int simple_failover(TestConnections* test)
{
    test->galera->connect();
    int rval = 0;
    bool blocked = false;
    char server_id[test->galera->N][1024];

    /** Get server_id for each node */
    for (int i = 0; i < test->galera->N; i++)
    {
        sprintf(server_id[i], "%d", test->galera->get_server_id(i));
    }

    do
    {
        /** Node 3 should be master */
        test->tprintf("Executing SELECT @@server_id, expecting '%s'...\n", server_id[2]);
        if (test->maxscales->connect_rwsplit(0) || check_server_id(test, server_id[2]))
        {
            test->tprintf("Test failed without any blocked nodes.\n");
            rval = 1;
            break;
        }
        test->maxscales->close_rwsplit(0);
        test->galera->block_node(2);
        blocked = true;
        test->tprintf("OK\n");
        sleep(15);

        /** Block node 3 and node 1 should be master */
        test->tprintf("Expecting '%s'...\n", server_id[0]);
        if (test->maxscales->connect_rwsplit(0) || check_server_id(test, server_id[0]))
        {
            test->tprintf("Test failed with first blocked node.\n");
            rval = 1;
            break;
        }
        test->maxscales->close_rwsplit(0);
        test->galera->block_node(0);
        test->tprintf("OK\n");
        sleep(15);

        /** Block node 1 and node 4 should be master */
        test->tprintf("Expecting '%s'...\n", server_id[3]);
        if (test->maxscales->connect_rwsplit(0) || check_server_id(test, server_id[3]))
        {
            test->tprintf("Test failed with second blocked node.\n");
            rval = 1;
            break;
        }
        test->maxscales->close_rwsplit(0);
        test->galera->block_node(3);
        test->tprintf("OK\n");
        sleep(15);

        /** Block node 4 and node 2 should be master */
        test->tprintf("Expecting '%s'...\n", server_id[1]);
        if (test->maxscales->connect_rwsplit(0) || check_server_id(test, server_id[1]))
        {
            test->tprintf("Test failed with third blocked node.\n");
            rval = 1;
            break;
        }
        test->maxscales->close_rwsplit(0);
        test->galera->block_node(1);
        test->tprintf("OK\n");
        sleep(15);

        /** All nodes blocked, expect failure */
        test->tprintf("Expecting failure...\n");
        int myerrno = 0;
        if ((myerrno = test->maxscales->connect_rwsplit(0)) == 0 && test->maxscales->conn_rwsplit[0])
        {
            test->tprintf("Connecting to rwsplit was expected to fail but it was"
                          " successful. Returned error was %d.\n", myerrno);
            if (execute_query(test->maxscales->conn_rwsplit[0], "SELECT @@server_id") == 0)
            {
                test->tprintf("SELECT @@server_id was expected to fail but the query was successful.\n");
            }
            else
            {
                test->tprintf("Connection succeeded but query failed.\n");
            }
            test->tprintf("Test failed with all nodes blocked.\n");
            rval = 1;
        }
        test->tprintf("OK\n");

        /** Unblock all nodes, node 3 should be master again */
        test->galera->unblock_all_nodes();
        blocked = false;
        sleep(15);
        test->tprintf("Expecting '%s'...\n", server_id[2]);
        if (test->maxscales->connect_rwsplit(0) || check_server_id(test, server_id[2]))
        {
            test->tprintf("Test failed after unblocking all nodes.\n");
            rval = 1;
            break;
        }
        test->maxscales->close_rwsplit(0);
        test->tprintf("OK\n");

        /** Restart MaxScale check that states are the same */
        test->maxscales->restart();
        sleep(15);
        test->tprintf("Expecting '%s'...", server_id[2]);
        if (test->maxscales->connect_rwsplit(0) || check_server_id(test, server_id[2]))
        {
            test->tprintf("Test failed after restarting MaxScale.");
            rval = 1;
            break;
        }
        test->maxscales->close_rwsplit(0);
        test->tprintf("OK\n");
    }
    while (false);

    if (blocked)
    {
        test->galera->unblock_all_nodes();
    }
    return rval;
}

int main(int argc, char **argv)
{
    TestConnections *test = new TestConnections(argc, argv);
    test->galera->verbose = false;
    int rval1 = 0;
    rval1 += simple_failover(test);
    int rval = test->global_result;
    delete test;
    return rval;
}
