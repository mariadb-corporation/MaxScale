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
 *  ilho Raatikka 2014-12-22 22:38:42 UTC
 *  Reproduce:
 *  1. connect readconnroute with mysql client
 *  2. fail the backend server
 *  3. execute query by using mysql client
 *
 *  >> client hangs if write to backend socket doesn't return error (which doesn't happen in many cases)
 *  Comment 1 Markus Mäkelä 2014-12-23 09:19:17 UTC
 *  Added a check for server status before routing the query. Now if the server is down it returns an error.
 */


#include <iostream>
#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(60);

    Test->tprintf("Connecting to Maxscale %s", Test->maxscales->IP[0]);
    Test->maxscales->connect_maxscale(0);

    printf("Setup firewall to block mysql on master");
    Test->repl->block_node(0);

    Test->tprintf(
        "Trying query to RWSplit, ReadConn master and ReadConn slave: expecting failure, but not a crash");
    execute_query(Test->maxscales->conn_rwsplit[0], "show processlist;");
    execute_query(Test->maxscales->conn_master[0], "show processlist;");
    execute_query(Test->maxscales->conn_slave[0], "show processlist;");
    Test->maxscales->close_maxscale_connections(0);

    // Wait three monitor intervals to allow the monitor to detect that the server is up
    Test->repl->unblock_node(0);
    sleep(3);

    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
