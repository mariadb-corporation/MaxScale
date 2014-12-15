/**
 * @file bug643.cpp  regression case for bugs 643 ("Hints, RWSplit: MaxScale goes into infinite loop and crashes") and bug645
 * - setup RWSplit in the following way forbug643
 * @verbatim
[RW Split Router]
type=service
router=readwritesplit
servers=server1,server2,server3,server4
max_slave_connections=100%
use_sql_variables_in=all
user=skysql
passwd=skysql
filters=testfi|duplicate

[duplicate]
type=filter
module=tee
service=RW Split Router
 @endverbatim
 * - for bug645
 * @verbatim
[RW_Router]
type=service
router=readconnroute
servers=server1
user=skysql
passwd=skysql
version_string=5.1-OLD-Bored-Mysql
filters=DuplicaFilter

[RW_Split]
type=service
router=readwritesplit
servers=server3,server2
user=skysql
passwd=skysql

[DuplicaFilter]
type=filter
module=tee
service=RW_Split

[RW_Listener]
type=listener
service=RW_Router
protocol=MySQLClient
port=4006
 @endverbatim
 * - try to connect
 * - try simple query
 * - check warnig in the log "Unable to find filter 'testfi' for service 'RW Split Router'"
 * - check MaxScale is alive
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
    global_result += execute_query(Test->conn_rwsplit, (char *) "show processlist");
    Test->CloseMaxscaleConn();

    global_result += CheckLogErr((char *) "Unable to find filter 'testfi' for service 'RW Split Router'", TRUE);

    global_result += CheckMaxscaleAlive();

    return(global_result);
}
