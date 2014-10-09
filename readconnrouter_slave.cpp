#include <iostream>
#include "testconnections.h"

using namespace std;

int main()
{
    TestConnections * Test = new TestConnections();
    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();

    MYSQL *conn[100];
    int i;
    int conn_num;
    printf("Creating 100 connections to ReadConnRouter in 'slave' mode\n");
    for (i=0; i<100; i++){
        conn[i] = Test->ConnectReadSlave();
    }
    printf("Waiting 10 seconds\n");
    sleep(10);
    printf("Checking connections to each node\n");
    for (int i = 0; i < Test->repl->N; i++) {
        conn_num = get_conn_num(Test->repl->nodes[0], Test->Maxscale_IP, (char *) "test");
        printf("Connections to node %d:\t%d\n", i, conn_num);
    }

    for (i=0; i<100; i++) { mysql_close(conn[i]); }
}



