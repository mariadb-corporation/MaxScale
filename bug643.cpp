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
 * - check warnig in the log "Error : RW Split Router: Recursive use of tee filter in service"
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    printf("Trying to connect to all Maxscale services\n"); fflush(stdout);
    Test->ConnectMaxscale();
    printf("Trying to send query to ReadConn master\n"); fflush(stdout);
    global_result += execute_query(Test->conn_master, (char *) "show processlist");
    printf("Trying to send query to ReadConn slave\n"); fflush(stdout);
    global_result += execute_query(Test->conn_slave, (char *) "show processlist");
    printf("Trying to send query to RWSplit, expecting failure\n"); fflush(stdout);
    if (execute_query(Test->conn_rwsplit, (char *) "show processlist") == 0) {
        global_result++;
        printf("FAIL: Query to broken service succeeded!\n");
    }
    Test->CloseMaxscaleConn();

    global_result += CheckLogErr((char *) "Error : RW Split Router: Recursive use of tee filter in service", TRUE);

    //global_result += CheckMaxscaleAlive();

    Test->Copy_all_logs(); return(global_result);
}
