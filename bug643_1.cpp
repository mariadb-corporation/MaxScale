/**
 * @file bug643.cpp  regression case for bugs 643 ("Hints, RWSplit: MaxScale goes into infinite loop and crashes") and bug645
 * - setup RWSplit in the following way for bug643
 * @verbatim
 [hints]
type=filter
module=hintfilter


[regex]
type=filter
module=regexfilter
match=fetch
replace=select

[typo]
type=filter
module=regexfilter
match=[Ff][Oo0][Rr][Mm]
replace=from

[qla]
type=filter
module=qlafilter
options=/tmp/QueryLog

[duplicate]
type=filter
module=tee
service=RW Split2

[testfilter]
type=filter
module=foobar

[RW Split Router]
type=service
router=readwritesplit
servers=server1,server2,server3,server4
#servers=server1,server2
max_slave_connections=100%
use_sql_variables_in=all
#use_sql_variables_in=master
user=skysql
passwd=skysql
#filters=typo|qla|regex|hints|regex|hints
#enable_root_user=1
filters=duplicate

[RW Split2]
type=service
router=readwritesplit
servers=server1,server2,server3,server4
max_slave_connections=100%
use_sql_variables_in=all
user=skysql
passwd=skysql
filters=qla|tests|hints

 @endverbatim
 * - try to connect
 * - try simple query using all services
 * - check warnig in the log "RW Split Router: Recursive use of tee filter in service"
 * - check if Maxscale still alive
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
    Test->CloseMaxscaleConn();

    global_result += CheckLogErr((char *) "RW Split Router: Recursive use of tee filter in service", TRUE);

    return(global_result);
}

