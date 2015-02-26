/**
 * @file rwsplit_conn_num.cpp Checks connections are distributed equaly
 * - create 100 connections to RWSplit
 * - check all slaves have equal number of connections
 * - check sum of number of connections to all slaves is equal to 100
 */


#include <my_config.h>
#include <iostream>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();

    const int TestConnNum = 100;
    MYSQL *conn[TestConnNum];
    int i;
    int conn_num;
    int res = 0;

    printf("Creating %d connections to RWSplit router\n", TestConnNum);
    for (i=0; i<TestConnNum; i++){
        conn[i] = Test->OpenRWSplitConn();
    }
    printf("Waiting 5 seconds\n");
    sleep(5);

    int ConnFloor = floor((float)TestConnNum / (Test->repl->N - 1));
    int ConnCell = ceil((float)TestConnNum / (Test->repl->N - 1));
    int TotalConn = 0;

    printf("Checking connections to Master: should be %d\n", TestConnNum);
    conn_num = get_conn_num(Test->repl->nodes[0], Test->Maxscale_IP, (char *) "test");
    if (conn_num != TestConnNum) {
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
            printf("FAILED: wrong number of connections to node %d\n", i);
        }
    }

    printf("Total number of connections %d\n", TotalConn);
    if (TotalConn != TestConnNum) {
        res++;
        printf("FAILED: total number of connections is wrong\n");

    }

    for (i=0; i<TestConnNum; i++) { mysql_close(conn[i]); }

    Test->Copy_all_logs(); return(res);
}




