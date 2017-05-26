/**
 * @file bug643.cpp  regression case for bugs 643 ("Hints, RWSplit: MaxScale goes into infinite loop and crashes") and bug645
 * - setup RWSplit in the following way for bug643
 * @verbatim
[RW Split Router]
type=service
router=readwritesplit
servers=server1,server2,server3,server4
max_slave_connections=100%
use_sql_variables_in=all
user=skysql
passwd=skysql
filters=duplicate

[duplicate]
type=filter
module=tee
service=RW Split Router
 @endverbatim
 * - try to connect
 * - try simple query using ReadConn router (both, master and slave)
 * - check warnig in the log "RW Split Router: Recursive use of tee filter in service"
 */

/*
Mark Riddoch 2014-12-11 11:59:19 UTC
There is a recursive use of the tee filter in the configuration.

The "RW Split Router" uses the"duplicate" filter that will then duplicate all traffic to the original destination and another copy of the "RW Split Router", which again will  duplicate all traffic to the original destination and another copy of the "RW Split Router"...

Really this needs to be trapped as a configuration error.
*/


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->tprintf("Trying to connect to all Maxscale services\n");
    fflush(stdout);
    Test->connect_maxscale();
    Test->tprintf("Trying to send query to ReadConn master\n");
    fflush(stdout);
    Test->try_query(Test->conn_master, (char *) "show processlist");
    Test->tprintf("Trying to send query to ReadConn slave\n");
    fflush(stdout);
    Test->try_query(Test->conn_slave, (char *) "show processlist");
    Test->tprintf("Trying to send query to RWSplit, expecting failure\n");
    fflush(stdout);
    if (execute_query(Test->conn_rwsplit, (char *) "show processlist") == 0)
    {
        Test->add_result(1, "FAIL: Query to broken service succeeded!\n");
    }
    Test->close_maxscale_connections();
    Test->check_log_err((char *) "RW-Split-Router: Recursive use of tee filter in service", true);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
