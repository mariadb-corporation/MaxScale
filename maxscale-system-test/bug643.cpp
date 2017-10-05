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
    Test->try_query(Test->maxscales->conn_master[0], (char *) "show processlist");
    Test->tprintf("Trying to send query to ReadConn slave\n");
    fflush(stdout);
    Test->try_query(Test->maxscales->conn_slave[0], (char *) "show processlist");
    Test->tprintf("Trying to send query to RWSplit, expecting failure\n");
    fflush(stdout);
    if (execute_query(Test->maxscales->conn_rwsplit[0], (char *) "show processlist") == 0)
    {
        Test->add_result(1, "FAIL: Query to broken service succeeded!\n");
    }
    Test->close_maxscale_connections();
    Test->check_log_err("Recursive use of tee filter in service", true);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
