/**
 * @file readconnrouter_master.cpp Connect to ReadConn in master mode and check if there is only one backend connection> to master
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


#include <my_config.h>
#include <iostream>
#include "testconnections.h"

using namespace std;

/**
 * @brief Checks if there is only one connection to master and no connections to other nodes
 * @param Test Pointer to TestConnections object that contains info about test setup
 * @param master Master node index
 * @return 0 if check succedded
 */
int CheckConnnectionsOnlyToMaster(TestConnections * Test, int master)
{
    int res = 0;
    int conn_num;
    printf("Checking number of connections to each node\n");
    for (int i = 0; i < Test->repl->N; i++) {
        conn_num = get_conn_num(Test->repl->nodes[i], Test->Maxscale_IP, (char *) "test");
        printf("Connections to node %d (%s):\t%d\n", i, Test->repl->IP[i], conn_num);
        if (((i == master) && (conn_num != 1)) || ((i != master) && (conn_num != 0))) {
            res++;
            printf("FAILED: number of connections to node %d is wrong\n", i);
        }
    }
    return(res);
}

int main(int argc, char *argv[])
{
    MYSQL * conn_read;
    int res = 0;

    TestConnections * Test = new TestConnections(argv[0]);
    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();

    printf("Connecting to ReadConnnRouter in 'master' mode\n");
    Test->ConnectReadMaster();
    printf("Sleeping 10 seconds\n");
    sleep(10);
    res += CheckConnnectionsOnlyToMaster(Test, 0);
    Test->CloseReadMaster();
    printf("Changing master to node 1\n");
    Test->repl->ChangeMaster(1, 0);
    printf("Sleeping 10 seconds\n");
    sleep(10);

    printf("Connecting to ReadConnnRouter in 'master' mode\n");
    Test->ConnectReadMaster();
    printf("Sleeping 10 seconds\n");
    sleep(10);
    res += CheckConnnectionsOnlyToMaster(Test, 1);
    Test->CloseReadMaster();

    printf("Changing master back to node 0\n");
    Test->repl->ChangeMaster(0, 1);

    res += CheckLogErr((char *) "The service 'CLI' is missing a definition of the servers", FALSE);

    Test->Copy_all_logs(); return(res);
}

