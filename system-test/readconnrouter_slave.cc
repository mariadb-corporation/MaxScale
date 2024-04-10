/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file readconnrouter_slave.cpp Creates 100 connections to ReadConn in slave mode and check if connections
 * are distributed among all slaves
 *
 * - create 100 connections to ReadConn slave
 * - check if all slave have equal number of connections (+-1)
 */

#include <maxtest/testconnections.hh>
#include <cmath>
#include <iostream>

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->reset_timeout();
    Test->repl->connect();

    const int TestConnNum = 100;
    MYSQL* conn[TestConnNum];
    int conn_num;

    Test->tprintf("Creating %d connections to ReadConnRouter in 'slave' mode\n", TestConnNum);
    for (int i = 0; i < TestConnNum; i++)
    {
        Test->reset_timeout();
        conn[i] = Test->maxscale->open_readconn_slave_connection();
        // This makes sure the connection is fully connected
        mysql_query(conn[i], "SET @a = 1");
    }

    int ConnFloor = floor((float)TestConnNum / (Test->repl->N - 1));
    int ConnCell = ceil((float)TestConnNum / (Test->repl->N - 1));
    int TotalConn = 0;

    Test->tprintf("Checking connections to Master: should be 0\n");
    conn_num = get_conn_num(Test->repl->nodes[0],
                            Test->maxscale->ip(),
                            Test->maxscale->hostname(),
                            (char*) "test");
    Test->add_result(conn_num, "number of connections to Master is %d\n", conn_num);

    Test->tprintf("Number of connections to each slave should be between %d and %d\n", ConnFloor, ConnCell);
    Test->tprintf("Checking connections to each node\n");
    for (int i = 1; i < Test->repl->N; i++)
    {
        conn_num =
            get_conn_num(Test->repl->nodes[i],
                         Test->maxscale->ip(),
                         Test->maxscale->hostname(),
                         (char*) "test");
        TotalConn += conn_num;
        printf("Connections to node %d (%s):\t%d\n", i, Test->repl->ip4(i), conn_num);
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

    for (int i = 0; i < TestConnNum; i++)
    {
        mysql_close(conn[i]);
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}
