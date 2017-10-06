/**
 * @file bug670.cpp bug670 regression case
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
 * check that Maxscale is alive, no crash
 */


/*

Description Vilho Raatikka 2014-12-30 11:54:52 UTC
Statement router (readwritesplit) loses pending statement if the other router executes statements faster than it. Statement router assumes that client doesn't send next statement before previous has replied. The only supported exception is session command which doesn't need to reply before client may send the next statement.

What happens in practice, is, that when 'next' statement arrives router's routeQuery it finds previous, still pending statement. In Debug build process traps. In Release build the pending command is overwritten.

(gdb) bt
#0  0x00007f050fa56065 in raise () from /lib64/libc.so.6
#1  0x00007f050fa574e8 in abort () from /lib64/libc.so.6
#2  0x00007f050fa4ef72 in __assert_fail_base () from /lib64/libc.so.6
#3  0x00007f050fa4f022 in __assert_fail () from /lib64/libc.so.6
#4  0x00007f050c06ddbb in route_single_stmt (inst=0x25ea750, rses=0x7f04d4002350, querybuf=0x7f04d400f130)
    at /home/raatikka/src/git/MaxScale/server/modules/routing/readwritesplit/readwritesplit.c:2372
#5  0x00007f050c06c2ef in routeQuery (instance=0x25ea750, router_session=0x7f04d4002350, querybuf=0x7f04d400f130)
    at /home/raatikka/src/git/MaxScale/server/modules/routing/readwritesplit/readwritesplit.c:1895
#6  0x00007f04f03ca573 in routeQuery (instance=0x7f04d4001f20, session=0x7f04d4002050, queue=0x7f04d400f130)
    at /home/raatikka/src/git/MaxScale/server/modules/filter/tee.c:597
#7  0x00007f04f8df700a in gw_read_client_event (dcb=0x7f04cc0009c0)
    at /home/raatikka/src/git/MaxScale/server/modules/protocol/mysql_client.c:867
#8  0x000000000058b351 in process_pollq (thread_id=4) at /home/raatikka/src/git/MaxScale/server/core/poll.c:858
#9  0x000000000058a9eb in poll_waitevents (arg=0x4) at /home/raatikka/src/git/MaxScale/server/core/poll.c:608
#10 0x00007f0511223e0f in start_thread () from /lib64/libpthread.so.0
#11 0x00007f050fb0a0dd in clone () from /lib64/libc.so.6
(gdb)
Comment 1 Vilho Raatikka 2014-12-30 12:04:06 UTC
Created attachment 171 [details]
Configuration.

1. Start MaxScale
2. feed session command/statement load to port 4008 which belongs to readconnrouter. Statements are then duplicated to rwsplit which starts to lag behind immediately.
Comment 2 Vilho Raatikka 2014-12-30 12:05:26 UTC
Created attachment 172 [details]
Simple script to run session command/statement load

Requires setmix.sql
Comment 3 Vilho Raatikka 2014-12-30 12:05:49 UTC
Created attachment 173 [details]
List of statements used by run_setmix.sh
Comment 4 Vilho Raatikka 2014-12-31 19:13:21 UTC
tee filter doesn't send reply to client before both maxscales->routers[0] have replied. This required adding upstream processing to tee filter. First reply is routed to client. By this tee ensures that new query is never sent to either router before they have replied to previous one.
Comment 5 Timofey Turenko 2015-01-08 12:40:34 UTC
test added, closing
Comment 6 Timofey Turenko 2015-02-28 18:11:16 UTC
Reopen: starting from 1.0.5 test for bug670 start to fail with debug assert:

(gdb) bt
#0  0x00007f542de05625 in raise () from /lib64/libc.so.6
#1  0x00007f542de06e05 in abort () from /lib64/libc.so.6
#2  0x00007f542ddfe74e in __assert_fail_base () from /lib64/libc.so.6
#3  0x00007f542ddfe810 in __assert_fail () from /lib64/libc.so.6
#4  0x00007f53fe7a2daf in clientReply (instance=0x7f53ec001970, session=0x7f53ec001aa0, reply=0x7f5404000b70)
    at /usr/local/skysql/maxscale/server/modules/filter/tee.c:973
#5  0x00007f54292c1b55 in clientReply (instance=0x335ae20, router_session=0x7f53ec000f90, queue=0x7f5404000b70,
    backend_dcb=0x7f53ec000fd0) at /usr/local/skysql/maxscale/server/modules/routing/readconnroute.c:814
#6  0x00007f54143f7fb7 in gw_read_backend_event (dcb=0x7f53ec000fd0)
    at /usr/local/skysql/maxscale/server/modules/protocol/mysql_backend.c:577
#7  0x000000000056962d in process_pollq (thread_id=3) at /usr/local/skysql/maxscale/server/core/poll.c:867
#8  0x0000000000568508 in poll_waitevents (arg=0x3) at /usr/local/skysql/maxscale/server/core/poll.c:608
#9  0x00007f542f54a9d1 in start_thread () from /lib64/libpthread.so.0
#10 0x00007f542debb8fd in clone () from /lib64/libc.so.6

*/



#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "bug670_sql.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    int i;

    Test->tprintf("Connecting to all MaxScale services\n");
    Test->add_result(Test->maxscales->connect_maxscale(0), "Error connecting to Maxscale\n");

    Test->tprintf("executing sql 100 times (ReadConn Slave)\n");
    for (i = 0; i < 100; i++)
    {
        Test->set_timeout(15);
        execute_query_silent(Test->maxscales->conn_slave[0], bug670_sql);
    }

    Test->tprintf("executing sql 100 times (ReadConn Master)\n");
    for (i = 0; i < 100; i++)
    {
        Test->set_timeout(15);
        execute_query_silent(Test->maxscales->conn_master[0], bug670_sql);
    }

    Test->tprintf("executing sql 100 times (RWSplit)\n");
    for (i = 0; i < 100; i++)
    {
        Test->set_timeout(15);
        execute_query_silent(Test->maxscales->conn_rwsplit[0], bug670_sql);
    }

    Test->set_timeout(10);
    Test->maxscales->close_maxscale_connections(0);

    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
