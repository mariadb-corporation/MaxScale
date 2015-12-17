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

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

using namespace std;

int check_server_id(TestConnections* test, char *node_id)
{
    char str[1024];
    int rval = 0;
    if (execute_query(test->conn_rwsplit, "BEGIN") ||
        find_field(test->conn_rwsplit, "SELECT @@server_id", "@@server_id", str) ||
        execute_query(test->conn_rwsplit, "COMMIT"))
    {
        printf("Failed to compare @@server_id.\n");
        rval = 1;
    }
    else if (strcmp(node_id, str))
    {
        printf("@@server_id is %s instead of %s\n", node_id, str);
        rval = 1;
    }
    return rval;
}

int simple_failover(TestConnections* test)
{
    test->galera->connect();
    int rval = 0;
    char server_id[test->galera->N][1024];

    /** Get server_id for each node */
    for (int i = 0; i < test->galera->N; i++)
    {
        sprintf(server_id[i], "%d", test->galera->get_server_id(i));
    }

    try
    {
        /** Node 3 should be master */
        if (test->connect_rwsplit() || check_server_id(test, server_id[2]))
        {
            throw;
        }
        test->close_rwsplit();
        test->galera->block_node(2);
        sleep(15);

        /** Block node 3 and node 1 should be master */
        if (test->connect_rwsplit() || check_server_id(test, server_id[0]))
        {
            throw;
        }
        test->close_rwsplit();
        test->galera->block_node(0);
        sleep(15);

        /** Block node 1 and node 4 should be master */
        if (test->connect_rwsplit() || check_server_id(test, server_id[3]))
        {
            throw;
        }
        test->close_rwsplit();
        test->galera->block_node(3);
        sleep(15);

        /** Block node 4 and node 2 should be master */
        if (test->connect_rwsplit() || check_server_id(test, server_id[1]))
        {
            throw;
        }
        test->close_rwsplit();
        test->galera->block_node(1);
        sleep(15);

        /** All nodes blocked, expect failure */
        if (test->connect_rwsplit() || execute_query(test->conn_rwsplit, "SELECT @@server_id") == 0)
        {
            printf("SELECT @@server_id was expected to fail but the query was successful.\n");
            throw;
        }
        test->close_rwsplit();
    }
    catch (...)
    {
        rval = 1;
        test->close_rwsplit();
    }

    test->galera->unblock_all_nodes();

    return rval;
}

int main(int argc, char **argv)
{
    TestConnections *test = new TestConnections(argc, argv);
    int rval = 0;
    rval += simple_failover(test);
    return rval;
}
