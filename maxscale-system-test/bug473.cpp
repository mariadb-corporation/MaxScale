/**
 * @file bug473.cpp  bug470, 472, 473 regression cases ( malformed hints cause crash )
 *
 * Test tries different hints with syntax errors (see source for details)
 */

/*
Markus Mäkelä 2014-08-07 09:21:44 UTC
All of the following queries cause a segmentation fault:

select @@server_id; -- maxscale route to server =(
select @@server_id; -- maxscale route to server =)
select @@server_id; -- maxscale route to server =:
select @@server_id; -- maxscale route to server =a
select @@server_id; -- maxscale route to server = a

Most likely all variatios with the equals sign and a character after it cause the crash.

Call stack:

#0  __strncasecmp_l_sse2 () at ../sysdeps/x86_64/strcmp.S:209
#1  0x00007fffdd0e9d0d in get_route_target (qtype=QUERY_TYPE_READ, trx_active=false, hint=0x74b830) at readwritesplit.c:1116
#2  0x00007fffdd0ea494 in routeQuery (instance=0x72f960, router_session=0x73dbf0, querybuf=0x74b7a0) at readwritesplit.c:1346
#3  0x00007fffd7191ed8 in routeQuery (instance=0x74b670, session=0x74b6b0, queue=0x74b7a0) at hintfilter.c:236
#4  0x00007fffdc2a0b3d in route_by_statement (session=0x744ae0, readbuf=0x0) at mysql_client.c:1442
#5  0x00007fffdc29f22c in gw_read_client_event (dcb=0x7446b0) at mysql_client.c:786
#6  0x00000000004165da in poll_waitevents (arg=0x0) at poll.c:424
#7  0x000000000040a72c in main (argc=4, argv=0x7fffffffe2e8) at gateway.c:1379

Failing point:

1114                            else if (hint->type == HINT_PARAMETER)
1115                            {
1116                                    if (strncasecmp(
1117                                            (char *)hint->data,
1118                                            "max_slave_replication_lag",
1119                                            strlen("max_slave_replication_lag")) == 0)
1120                                    {


Value of hint:

$6 = {type = HINT_PARAMETER, data = 0x0, value = 0x743a60, dsize = 0, next = 0x0}
*/



#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->add_result(Test->connect_maxscale(), "Can not connect to Maxscale\n");


    Test->tprintf("Trying queries that caused crashes before fix: bug473\n");

    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "select @@server_id; -- maxscale route to server =(");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "select @@server_id; -- maxscale route to server =)");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "select @@server_id; -- maxscale route to server =:");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "select @@server_id; -- maxscale route to server =a");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "select @@server_id; -- maxscale route to server = a");
    Test->try_query(Test->maxscales->conn_rwsplit[0],
                    (char *) "select @@server_id; -- maxscale route to server = кириллица åäö");

    // bug472
    Test->tprintf("Trying queries that caused crashes before fix: bug472\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0],
                    (char *) "select @@server_id; -- maxscale s1 begin route to server server3");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "select @@server_id; -- maxscale end");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "select @@server_id; -- maxscale s1 begin");

    // bug470
    Test->tprintf("Trying queries that caused crashes before fix: bug470\n");
    fflush(stdout);
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "select @@server_id; -- maxscale named begin route to master");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "select @@server_id;");
    Test->try_query(Test->maxscales->conn_rwsplit[0],
                    (char *) "select @@server_id; -- maxscale named begin route to master; select @@server_id;");


    Test->close_maxscale_connections();

    Test->tprintf("Checking if Maxscale is alive\n");
    fflush(stdout);
    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}

