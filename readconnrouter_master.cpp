#include <iostream>
#include "testconnections.h"

using namespace std;

int CheckConnnctionsOnlyToMaster(TestConnections * Test, int master)
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
}

int main()
{
    MYSQL * conn_read;
    int res = 0;

    TestConnections * Test = new TestConnections();
    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();

    printf("Connecting to ReadConnnRouter in 'master' mode\n");
    conn_read = Test->ConnectReadMaster();
    printf("Sleeping 10 seconds\n");
    sleep(10);
    res += CheckConnnctionsOnlyToMaster(Test, 0);
    mysql_close(conn_read);

    printf("Changing master to node 1\n");
    Test->repl->ChangeMaster(1, 0);
    printf("Sleeping 10 seconds\n");
    sleep(10);

    printf("Connecting to ReadConnnRouter in 'master' mode\n");
    conn_read = Test->ConnectReadMaster();
    printf("Sleeping 10 seconds\n");
    sleep(10);
    res += CheckConnnctionsOnlyToMaster(Test, 1);
    mysql_close(conn_read);

    printf("Changing master back to node 0\n");
    Test->repl->ChangeMaster(0, 1);

    return(res);
}

