/**
 * @file readconnrouter_slave.cpp Creates 100 connections to ReadConn in slave mode and check if connections are distributed among all slaves
 *
 * - create 100 connections to ReadConn slave
 * - check if all slave have equal number of connections (+-1)
 */


#include <iostream>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(100);
    Test->repl->connect();

    const int TestConnNum = 100;
    MYSQL *conn[TestConnNum];
    int i;
    int conn_num;

    Test->tprintf("Creating %d connections to ReadConnRouter in 'slave' mode\n", TestConnNum);
    for (i = 0; i < TestConnNum; i++)
    {
        conn[i] = Test->maxscales->open_readconn_slave_connection(0);
    }
    Test->tprintf("Waiting 5 seconds\n");
    sleep(5);

    int ConnFloor = floor((float)TestConnNum / (Test->repl->N - 1));
    int ConnCell = ceil((float)TestConnNum / (Test->repl->N - 1));
    int TotalConn = 0;

    Test->tprintf("Checking connections to Master: should be 0\n");
    conn_num = get_conn_num(Test->repl->nodes[0], Test->maxscales->ip(0), Test->maxscales->hostname[0], (char *) "test");
    Test->add_result(conn_num, "number of connections to Master is %d\n", conn_num);

    Test->tprintf("Number of connections to each slave should be between %d and %d\n", ConnFloor, ConnCell);
    Test->tprintf("Checking connections to each node\n");
    for (int i = 1; i < Test->repl->N; i++)
    {
        conn_num = get_conn_num(Test->repl->nodes[i], Test->maxscales->ip(0), Test->maxscales->hostname[0], (char *) "test");
        TotalConn += conn_num;
        printf("Connections to node %d (%s):\t%d\n", i, Test->repl->IP[i], conn_num);
        if ((conn_num > ConnCell) || (conn_num < ConnFloor))
        {
            Test->add_result(1, "wrong number of connectiosn to mode %d\n", i);
        }
    }

    Test->tprintf("Total number of connections %d\n", TotalConn);
    if (TotalConn != TestConnNum)
    {
        Test->add_result(1, "total number of connections is wrong\n");
    }

    for (i = 0; i < TestConnNum; i++)
    {
        mysql_close(conn[i]);
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}



