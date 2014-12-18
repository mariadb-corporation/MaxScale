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
 * - check warnig in the log "Error : Failed to start service 'RW Split2'"
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    Test->ConnectMaxscale();
    global_result += execute_query(Test->conn_master, (char *) "show processlist");
    global_result += execute_query(Test->conn_slave, (char *) "show processlist");
    global_result += execute_query(Test->conn_rwsplit, (char *) "show processlist");
    Test->CloseMaxscaleConn();

    global_result += CheckLogErr((char *) "Error : Failed to start service 'RW Split2'", TRUE);

    global_result += CheckMaxscaleAlive();

    return(global_result);
}
