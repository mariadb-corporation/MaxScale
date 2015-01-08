/**
 * @file bug670.cpp bug670 regression case ( Executing '\s' doesn't always produce complete result set)
 * configuration
 * @verbatim
[MySQL Monitor]
type=monitor
module=mysqlmon
monitor_interval=10000
servers=server1,server2,server3,server4
detect_replication_lag=1
detect_stale_master=1
user=maxuser
passwd=maxpwd

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
#servers=server1
max_slave_connections=100%
use_sql_variables_in=all
#use_sql_variables_in=master
user=maxuser
passwd=maxpwd
filters=typo|qla|regex|hints|regex|hints
enable_root_user=1

[RW Split2]
type=service
router=readwritesplit
servers=server1,server2
max_slave_connections=100%
use_sql_variables_in=all
user=maxuser
passwd=maxpwd
#filters=qla|tests|hints

[Read Connection Router]
type=service
router=readconnroute
router_options=slave
servers=server1,server2
user=maxuser
passwd=maxpwd
filters=duplicate

[HTTPD Router]
type=service
router=testroute
servers=server1,server2,server3

[Debug Interface]
type=service
router=debugcli

[CLI]
type=service
router=cli

[RW Split Listener]
type=listener
service=RW Split Router
protocol=MySQLClient
port=4006
#socket=/tmp/rwsplit.sock

[RW Split Listener2]
type=listener
service=RW Split2
protocol=MySQLClient
port=4012

[Read Connection Listener]
type=listener
service=Read Connection Router
protocol=MySQLClient
port=4008
#socket=/tmp/readconn.sock

 @endverbatim
 * execute following SQLs against all services in the loop (100 times)
 * @verbatim
set autocommit=0;
use mysql;
set autocommit=1;
use test;
set autocommit=0;
use mysql;
set autocommit=1;
select user,host from user;
set autocommit=0;
use fakedb;
use test;
use mysql;
use dontuse;
use mysql;
drop table if exists t1;
commit;
use test;
use mysql;
set autocommit=1;
create table t1(id integer primary key);
insert into t1 values(5);
use test;
use mysql;
select user from user;
set autocommit=0;
set autocommit=1;
set autocommit=0;
insert into mysql.t1 values(7);
use mysql;
rollback work;
commit;
delete from mysql.t1 where id=7;
insert into mysql.t1 values(7);
select host,user from mysql.user;
set autocommit=1;
delete from mysql.t1 where id = 7;
select 1 as "endof cycle" from dual;
 @endverbatim
 *
 */


#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "bug670_sql.h"

using namespace std;

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;
    int i;

    Test->ReadEnv();
    Test->PrintIP();

    printf("Connecting to all MaxScale services\n"); fflush(stdout);
    global_result += Test->ConnectMaxscale();

    printf("executing sql 100 times (ReadConn Slave)\n"); fflush(stdout);
    for (i = 0; i < 100; i++)  {
        execute_query(Test->conn_slave, bug670_sql);
    }

    printf("executing sql 100 times (ReadConn Master)\n"); fflush(stdout);
    for (i = 0; i < 100; i++)  {
        execute_query(Test->conn_master, bug670_sql);
    }

    printf("executing sql 100 times (RWSplit)\n"); fflush(stdout);
    for (i = 0; i < 100; i++)  {
        execute_query(Test->conn_rwsplit, bug670_sql);
    }

    Test->CloseMaxscaleConn();

    global_result += CheckMaxscaleAlive();

    return(global_result);
}
