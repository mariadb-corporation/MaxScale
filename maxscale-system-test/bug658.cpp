/**
 * @file bug658.cpp regression case for bug 658 ("readconnroute: client is not closed if backend fails")
 *
 * - Connect all MaxScale
 * - block Mariadb server on Master node by Firewall
 * - execute query
 * - unblock Mariadb server
 * - do same test, but block all backend nodes
 * - check if Maxscale is alive
 */

/*
ilho Raatikka 2014-12-22 22:38:42 UTC
Reproduce:
1. connect readconnroute with mysql client
2. fail the backend server
3. execute query by using mysql client

>> client hangs if write to backend socket doesn't return error (which doesn't happen in many cases)
Comment 1 Markus Mäkelä 2014-12-23 09:19:17 UTC
Added a check for server status before routing the query. Now if the server is down it returns an error.
*/


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(40);
    int i;

    Test->tprintf("Connecting to Maxscale %s\n", Test->maxscales->IP[0]);
    Test->maxscales->connect_maxscale(0);

    printf("Setup firewall to block mysql on master\n");
    fflush(stdout);
    Test->repl->block_node(0);

    sleep(1);

    Test->tprintf("Trying query to RWSplit, expecting failure, but not a crash\n");
    execute_query(Test->maxscales->conn_rwsplit[0], (char *) "show processlist;");
    fflush(stdout);
    Test->tprintf("Trying query to ReadConn master, expecting failure, but not a crash\n");
    execute_query(Test->maxscales->conn_master[0], (char *) "show processlist;");
    fflush(stdout);
    Test->tprintf("Trying query to ReadConn slave, expecting failure, but not a crash\n");
    execute_query(Test->maxscales->conn_slave[0], (char *) "show processlist;");
    fflush(stdout);

    sleep(1);

    Test->repl->unblock_node(0);
    sleep(10);

    Test->maxscales->close_maxscale_connections(0);

    Test->tprintf("Checking Maxscale is alive\n");
    Test->check_maxscale_alive(0);

    Test->set_timeout(20);

    Test->tprintf("Connecting to Maxscale %s to check its behaviour in case of blocking all bacxkends\n",
                  Test->maxscales->IP[0]);
    Test->maxscales->connect_maxscale(0);

    if (!Test->smoke)
    {
        for (i = 0; i < Test->repl->N; i++)
        {
            Test->tprintf("Setup firewall to block mysql on node %d\n", i);
            Test->repl->block_node(i);
            fflush(stdout);
        }
        sleep(1);

        Test->tprintf("Trying query to RWSplit, expecting failure, but not a crash\n");
        execute_query(Test->maxscales->conn_rwsplit[0], (char *) "show processlist;");
        fflush(stdout);
        Test->tprintf("Trying query to ReadConn master, expecting failure, but not a crash\n");
        execute_query(Test->maxscales->conn_master[0], (char *) "show processlist;");
        fflush(stdout);
        Test->tprintf("Trying query to ReadConn slave, expecting failure, but not a crash\n");
        execute_query(Test->maxscales->conn_slave[0], (char *) "show processlist;");
        fflush(stdout);

        sleep(1);

        for (i = 0; i < Test->repl->N; i++)
        {
            Test->tprintf("Setup firewall back to allow mysql on node %d\n", i);
            Test->repl->unblock_node(i);
            fflush(stdout);
        }
    }
    Test->stop_timeout();
    Test->tprintf("Sleeping 20 seconds\n");
    sleep(20);

    Test->set_timeout(20);

    Test->maxscales->close_maxscale_connections(0);
    Test->tprintf("Checking Maxscale is alive\n");
    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
