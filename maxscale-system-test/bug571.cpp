/**
 * @file bug571.cpp  regression case for bug 571 and bug 585 ( "Using regex filter hangs MaxScale" and "modutil_extract_SQL doesn't work with multiple GWBUF buffers" )
 *
 * - Maxscale.cnf
 * @verbatim
 [regex]
 type=filter
 module=regexfilter
 match=[Ff][Oo0][rR][mM]
 replace=FROM

 [r2]
 type=filter
 module=regexfilter
 match=fetch
 replace=select

 [hints]
 type=filter
 module=hintfilter

 [RW Split Router]
 type=service
 router= readwritesplit
 servers=server1,     server2,              server3,server4
 user=skysql
 passwd=skysql
 max_slave_connections=100%
 use_sql_variables_in=all
 router_options=slave_selection_criteria=LEAST_BEHIND_MASTER
 filters=hints|regex|r2
 @endverbatim
 * for bug585:
 * @verbatim
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

[RW Split Router]
type=service
router= readwritesplit
servers=server1,     server2,              server3,server4
user=skysql
passwd=skysql
max_slave_connections=100%
use_sql_variables_in=all
router_options=slave_selection_criteria=LEAST_BEHIND_MASTER
filters=regex|typo
 @endverbatim
 * - fetch * from mysql.user;
 * - fetch count(*) form mysql.user;
 * - check if Maxscale is alive
 */

/*
Vilho Raatikka 2014-10-10 11:09:19 UTC
Branch:release-1.0.1beta
Executing :

fetch * from mysql.user

with this config hangs MaxScale

[regex]
type=filter
module=regexfilter
match=[Ff][Oo0][rR][mM]
replace=FROM

[r2]
type=filter
module=regexfilter
match=fetch
replace=select


[hints]
type=filter
module=hintfilter


[RW Split Router]
type=service
router=readwritesplit
servers=server1,server2,server3,server4
max_slave_connections=100%
use_sql_variables_in=all
router_options=slave_selection_criteria=LEAST_BEHIND_MASTER
user=maxuser
passwd=maxpwd
filters=hints|regex|r2
Comment 1 Vilho Raatikka 2014-10-11 18:55:23 UTC
If we look at the rewrite function we see that query lacks one character.

T 127.0.0.1:37858 -> 127.0.0.1:4006 [AP]
  16 00 00 00 03 66 65 74    63 68 20 69 64 20 66 72    .....fetch id fr
  6f 6d 20 74 65 73 74 2e    74 31                      om test.t1

T 127.0.0.1:44591 -> 127.0.0.1:3000 [AP]
  17 00 00 00 03 73 65 6c    65 63 74 20 69 64 20 66    .....select id f
  72 6f 6d 20 74 65 73 74    2e 74                      rom test.t
*/

// the same code is used for bug585


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->tprintf("Connecting to all MaxScale services\n");
    Test->set_timeout(10);
    Test->add_result(Test->maxscales->connect_maxscale(0), "Error connectiong to Maxscale\n");

    Test->tprintf("executing fetch * from mysql.user \n");
    Test->set_timeout(10);
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "fetch * from mysql.user;");
    Test->set_timeout(10);
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "fetch count(*) form mysql.user;");

    Test->set_timeout(10);
    Test->maxscales->close_maxscale_connections(0);
    Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
