/**
 * @file bug422.cpp bug422 regression case ( Executing '\s' doesn't always produce complete result set)
 *
 * Test executes "show status" 1000 times against all Maxscale services and checks Maxscale is alive after it.
 */

/*
Vilho Raatikka 2014-04-15 07:03:03 UTC
Read/write split router
-------------------------

Login to MaxScale & read/write split router, for example

mysql --host=127.0.0.1 -P 4006 -u maxuser -pmaxpwd

Complete result :

MySQL [(none)]> \s
--------------
mysql  Ver 15.1 Distrib 5.5.33-MariaDB, for Linux (x86_64) using readline 5.1

Connection id:          4051
Current database:
Current user:           maxuser@localhost
SSL:                    Not in use
Current pager:          less
Using outfile:          ''
Using delimiter:        ;
Server:                 MySQL
Server version:         MaxScale 0.5.0 Source distribution
Protocol version:       10
Connection:             127.0.0.1 via TCP/IP
Server characterset:    latin1
Db     characterset:    latin1
Client characterset:    latin1
Conn.  characterset:    latin1
TCP port:               4006
Uptime:                 34 min 23 sec

Threads: 5  Questions: 206  Slow queries: 0  Opens: 0  Flush tables: 2  Open tables: 26  Queries per second avg: 0.099
--------------


By running same a few time in a row, an incomplete result set arrives, like the following:

MySQL [(none)]> \s
--------------
mysql  Ver 15.1 Distrib 5.5.33-MariaDB, for Linux (x86_64) using readline 5.1

Connection id:          4051
Current database:
Current user:           maxuser@localhost
SSL:                    Not in use
Current pager:          less
Using outfile:          ''
Using delimiter:        ;
Server:                 MySQL
Server version:         MaxScale 0.5.0 Source distribution
Protocol version:       10
Connection:             127.0.0.1 via TCP/IP
Server characterset:    latin1
Db     characterset:    latin1
Client characterset:    latin1
Conn.  characterset:    latin1
TCP port:               4006
--------------

MySQL [(none)]>

*/


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int i;
    int iterations = 1000;
    if (Test->smoke)
    {
        iterations = 100;
    }

    Test->set_timeout(10);

    Test->tprintf("Connecting to all MaxScale services\n");
    Test->add_result(Test->connect_maxscale(), "Can not connect to Maxscale\n");

    Test->tprintf("executing show status %d times\n", iterations);


    for (i = 0; i < iterations; i++)
    {
        Test->set_timeout(5);
        Test->add_result(execute_query(Test->maxscales->conn_rwsplit[0], (char *) "show status"),
                         "Query %d agains RWSplit failed\n", i);
    }
    for (i = 0; i < iterations; i++)
    {
        Test->set_timeout(5);
        Test->add_result(execute_query(Test->maxscales->conn_slave[0], (char *) "show status"),
                         "Query %d agains ReadConn Slave failed\n", i);
    }
    for (i = 0; i < iterations; i++)
    {
        Test->set_timeout(5);
        Test->add_result(execute_query(Test->maxscales->conn_master[0], (char *) "show status"),
                         "Query %d agains ReadConn Master failed\n", i);
    }
    Test->set_timeout(10);

    Test->close_maxscale_connections();
    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
