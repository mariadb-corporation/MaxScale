/**
 * @file connection_limit.cpp  connection_limit check if max_connections parameter works
 *
 * - Maxscale.cnf contains max_connections=10 for RWSplit, max_connections=20 for ReadConn master and max_connections=25 for ReadConn slave
 * - create max num of connections and check tha N+1 connection fails
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

void check_max_conn(int router, int max_conn, TestConnections * Test)
{
    MYSQL * conn[max_conn + 1];

    int i;
    for (i = 0; i < max_conn; i++)
    {
        conn[i] = open_conn(Test->maxscales->ports[0][router], Test->maxscales->IP[0], Test->maxscales->user_name,
                            Test->maxscales->password,
                            Test->ssl);
        if (mysql_errno(conn[i]) != 0)
        {
            Test->add_result(1, "Connection %d failed, error is %s\n", i, mysql_error(conn[i]));
        }
    }
    conn[max_conn] = open_conn(Test->maxscales->ports[0][router], Test->maxscales->IP[0],
                               Test->maxscales->user_name,
                               Test->maxscales->password, Test->ssl);
    if (mysql_errno(conn[i]) != 1040)
    {
        Test->add_result(1, "Max_xonnections reached, but error is not 1040, it is %d %s\n", mysql_errno(conn[i]),
                         mysql_error(conn[i]));
    }
    for (i = 0; i < max_conn; i++)
    {
        mysql_close(conn[i]);
    }
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->tprintf("Trying 11 connections with RWSplit\n");
    check_max_conn(0, 10, Test);
    Test->tprintf("Trying 21 connections with Readconn master\n");
    check_max_conn(1, 20, Test);
    Test->tprintf("Trying 26 connections with Readconnn slave\n");
    check_max_conn(2, 25, Test);

    sleep(10);

    Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
