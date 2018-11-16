/**
 * @file readconnrouter_master.cpp Connect to ReadConn in master mode and check if there is only one backend
 * connection to master
 *
 * - connect to ReadCon master
 * - expect only 1 connection to node 0 and no connections to ther nodes
 * - close connections
 * - change master to node 1
 * - connect again
 * - expect only 1 connection to node 1 and no connections to ther nodes
 * - close connection
 * - change master back to node 0
 */


#include <iostream>
#include "testconnections.h"

using namespace std;

/**
 * @brief Checks if there is only one connection to master and no connections to other nodes
 * @param Test Pointer to TestConnections object that contains info about test setup
 * @param master Master node index
 * @return 0 if check succedded
 */
int check_connnections_only_to_master(TestConnections* Test, int master)
{
    int res = 0;
    int conn_num;
    printf("Checking number of connections to each node\n");
    for (int i = 0; i < Test->repl->N; i++)
    {
        conn_num =
            get_conn_num(Test->repl->nodes[i],
                         Test->maxscales->ip(0),
                         Test->maxscales->hostname[0],
                         (char*) "test");
        printf("Connections to node %d (%s):\t%d\n", i, Test->repl->IP[i], conn_num);
        if (((i == master) && (conn_num != 1)) || ((i != master) && (conn_num != 0)))
        {
            res++;
            printf("FAILED: number of connections to node %d is wrong\n", i);
        }
    }
    return res;
}

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(100);

    Test->repl->connect();

    Test->tprintf("Connecting to ReadConnnRouter in 'master' mode\n");
    Test->maxscales->connect_readconn_master(0);
    printf("Sleeping 10 seconds\n");
    Test->stop_timeout();
    sleep(10);
    Test->set_timeout(50);
    Test->add_result(check_connnections_only_to_master(Test, 0), "connections are not only to Master\n");
    Test->maxscales->close_readconn_master(0);
    Test->tprintf("Changing master to node 1\n");
    Test->set_timeout(50);
    Test->repl->change_master(1, 0);
    printf("Sleeping 10 seconds\n");
    Test->stop_timeout();
    sleep(10);
    Test->set_timeout(50);
    printf("Connecting to ReadConnnRouter in 'master' mode\n");
    Test->maxscales->connect_readconn_master(0);
    printf("Sleeping 10 seconds\n");
    Test->stop_timeout();
    sleep(10);
    Test->set_timeout(50);
    Test->add_result(check_connnections_only_to_master(Test, 1), "connections are not only to master");
    Test->maxscales->close_readconn_master(0);
    Test->set_timeout(50);
    printf("Changing master back to node 0\n");
    Test->repl->change_master(0, 1);

    Test->log_excludes(0, "The service 'CLI' is missing a definition of the servers");

    int rval = Test->global_result;
    delete Test;
    return rval;
}
