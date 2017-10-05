/**
 * @file bug469 bug469 regression test case ("rwsplit counts every connection twice in master - counnection counts leak")
 * - use maxadmin command "show server server1" and check "Current no. of conns" and "Number of connections" - both should be 0
 * - execute simple query against RWSplit
 * - use maxadmin command "show server server1" and check "Current no. of conns" (should be 0) and "Number of connections" (should be 1)
 */


/*
Vilho Raatikka 2014-08-05 12:28:21 UTC
Every connection is counted twice in master and decremented only once. As a result master seems always to have active connections after first connection is established.

Server 0x21706e0 (server1)
        Server:                         127.0.0.1
        Status:                         Master, Running
        Protocol:                       MySQLBackend
        Port:                           3000
        Server Version:                 5.5.37-MariaDB-debug-log
        Node Id:                        3000
        Master Id:                      -1
        Slave Ids:                      3001, 3002 , 3003
        Repl Depth:                     0
        Number of connections:          6
        Current no. of conns:           3
        Current no. of operations:      0
Server 0x21705e0 (server2)
        Server:                         127.0.0.1
        Status:                         Slave, Running
        Protocol:                       MySQLBackend
        Port:                           3001
        Server Version:                 5.5.37-MariaDB-debug-log
        Node Id:                        3001
        Master Id:                      3000
        Slave Ids:
        Repl Depth:                     1
        Number of connections:          3
        Current no. of conns:           0
        Current no. of operations:      0

*/


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    char res[1024];
    int res_d;
    Test->set_timeout(10);

    Test->get_maxadmin_param((char *) "show server server1", (char *) "Current no. of conns:", res);
    sscanf(res, "%d", &res_d);
    Test->tprintf("Before: Current num of conn %d\n", res_d);
    Test->add_result(res_d, "curr num of conn is not 0\n");
    Test->get_maxadmin_param((char *) "show server server1", (char *) "Number of connections:", res);
    sscanf(res, "%d", &res_d);
    Test->tprintf("Before: num of conn %d\n", res_d);
    Test->add_result(res_d, "num of conn is not 0");

    Test->connect_rwsplit();
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "select 1");
    Test->close_rwsplit();

    Test->stop_timeout();
    sleep(10);

    Test->set_timeout(10);

    Test->get_maxadmin_param((char *) "show server server1", (char *) "Current no. of conns:", res);
    sscanf(res, "%d", &res_d);
    Test->tprintf("After: Current num of conn %d\n", res_d);
    Test->add_result(res_d, "curr num of conn is not 0\n");
    Test->get_maxadmin_param((char *) "show server server1", (char *) "Number of connections:", res);
    sscanf(res, "%d", &res_d);
    Test->tprintf("After: num of conn %d\n", res_d);
    if (res_d != 1)
    {
        Test->add_result(1, "num of conn is not 1");
    }

    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
