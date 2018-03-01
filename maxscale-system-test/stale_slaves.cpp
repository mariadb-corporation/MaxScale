/**
 * @file stale_slaves.cpp Testing slaves who have lost their master and how MaxScale works with them
 *
 * When the master server is blocked and slaves lose their master, they should
 * still be available for read queries. When a slave with no master fails, it should not
 * be assigned slave status again. Once the master comes back, all slaves should get slave
 * status if replication is running.
 */


#include <iostream>
#include "testconnections.h"

int main(int argc, char **argv)
{
    TestConnections *test = new TestConnections(argc, argv);

    char server_id[test->repl->N][1024];

    test->repl->connect();
    /** Get server_id for each node */
    for (int i = 0; i < test->repl->N; i++)
    {
        sprintf(server_id[i], "%d", test->repl->get_server_id(i));
    }

    test->tprintf("Block the master and try a read query\n");
    test->repl->block_node(0);
    sleep(15);
    test->connect_readconn_slave();
    char first_slave[1024];
    find_field(test->conn_slave, "SELECT @@server_id", "@@server_id", first_slave);

    int found = -1;

    for (int i = 0; i < test->repl->N; i++)
    {
        if (strcmp(server_id[i], first_slave) == 0)
        {
            found = i;
            break;
        }
    }

    test->add_result(found < 0, "No server with ID '%s' found.", first_slave);

    test->tprintf("Blocking node %d\n", found + 1);
    test->repl->block_node(found);
    sleep(15);

    test->tprintf("Blocked the slave that replied to us, expecting a different slave\n");
    test->connect_readconn_slave();
    char second_slave[1024];
    find_field(test->conn_slave, "SELECT @@server_id", "@@server_id", second_slave);
    test->add_result(strcmp(first_slave, second_slave) == 0,
                     "Server IDs match when they shouldn't: %s - %s",
                     first_slave, second_slave);

    test->tprintf("Unblocking the slave that replied\n");
    test->repl->unblock_node(found);
    sleep(15);

    test->tprintf("Unblocked the slave, still expecting a different slave\n");
    test->connect_readconn_slave();
    find_field(test->conn_slave, "SELECT @@server_id", "@@server_id", second_slave);
    test->add_result(strcmp(first_slave, second_slave) == 0,
                     "Server IDs match when they shouldn't: %s - %s",
                     first_slave, second_slave);

    test->tprintf("Unblocking all nodes\n");
    test->repl->unblock_all_nodes();
    sleep(15);

    test->tprintf("Unblocked all nodes, expecting the server ID of the first slave server\n");
    test->connect_readconn_slave();
    find_field(test->conn_slave, "SELECT @@server_id", "@@server_id", second_slave);
    test->add_result(strcmp(first_slave, second_slave) != 0,
                     "Server IDs don't match when they should: %s - %s",
                     first_slave, second_slave);

    test->tprintf("Stopping replication on node %d\n", found + 1);
    execute_query(test->repl->nodes[found], "stop slave");
    sleep(15);

    test->tprintf("Stopped replication, expecting a different slave\n");
    test->connect_readconn_slave();
    find_field(test->conn_slave, "SELECT @@server_id", "@@server_id", second_slave);
    test->add_result(strcmp(first_slave, second_slave) == 0,
                     "Server IDs match when they shouldn't: %s - %s",
                     first_slave, second_slave);

    test->tprintf("Starting replication on node %d\n", found + 1);
    execute_query(test->repl->nodes[found], "start slave");
    sleep(15);

    test->tprintf("Started replication, expecting the server ID of the first slave server\n");
    test->connect_readconn_slave();
    find_field(test->conn_slave, "SELECT @@server_id", "@@server_id", second_slave);
    test->add_result(strcmp(first_slave, second_slave) != 0,
                     "Server IDs don't match when they should: %s - %s",
                     first_slave, second_slave);

    int rval = test->global_result;
    delete test;
    return rval;
}
