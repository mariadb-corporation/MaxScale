#include <iostream>
#include "testconnections.h"

using namespace std;

int main()
{
    TestConnections * Test = new TestConnections();

    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();

    const int TestConnNum = 100;
    MYSQL *conn[TestConnNum];
    int i;
    int conn_num;
    int res = 0;

    printf("Creating %d connections to ReadConnRouter in 'slave' mode\n", TestConnNum);
    for (i=0; i<TestConnNum; i++){
        conn[i] = Test->ConnectReadSlave();
    }
    printf("Waiting 5 seconds\n");
    sleep(5);

    int ConnFloor = floor((float)TestConnNum / Test->repl->N);
    int ConnCell = floor((float)TestConnNum / Test->repl->N);
    int TotalConn = 0;

    printf("Checking connections to Master: should be 0\n");
    conn_num = get_conn_num(Test->repl->nodes[0], Test->Maxscale_IP, (char *) "test");
    if (conn_num != 0) {
        res++;
        printf("FAILED: number of connections to Master is %d\n", conn_num);
    }

    printf("Number of connections to each slave should be between %d and %d\n", ConnFloor, ConnCell);
    printf("Checking connections to each node\n");
    for (int i = 1; i < Test->repl->N; i++) {
        conn_num = get_conn_num(Test->repl->nodes[i], Test->Maxscale_IP, (char *) "test");
        TotalConn += conn_num;
        printf("Connections to node %d (%s):\t%d\n", i, Test->repl->IP[i], conn_num);
        if ((conn_num > ConnCell) || (conn_num < ConnFloor)) {
            res++;
            printf("FAILED: wrong number of connectiosn to mode %d\n", i);
        }
    }

    printf("Total number of connections %d\n", conn_num);
    if (conn_num != TestConnNum) {
        res++;
        printf("FAILED: total number of connections is wrong\n");

    }

    for (i=0; i<TestConnNum; i++) { mysql_close(conn[i]); }

    return(res);
}



