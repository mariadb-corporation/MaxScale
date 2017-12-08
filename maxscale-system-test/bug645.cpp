/**
 * @file bug643.cpp  regression case for bugs 645 ("Tee filter with readwritesplit service hangs MaxScale")
 * - setup RWSplit in the following way
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
servers=server1, server3,server2
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

[RW_Split_list]
type=listener
service=RW_Split
protocol=MySQLClient
port=4016

 @endverbatim
 * - try to connect
 * - try simple query
 * - check MaxScale is alive
 */

/*
Massimiliano 2014-12-11 14:19:51 UTC
When tee filter is used with a readwritesplit service MaxScale hangs (each service including admin interface)or there is a failed assetion in Debug mode:

debug assert /source/GA/server/modules/routing/readwritesplit/readwritesplit.c:1825
maxscale: /source/GA/server/modules/routing/readwritesplit/readwritesplit.c:1825: routeQuery: Assertion `!(querybuf->gwbuf_type == 0)' failed.


Configuration:


[RW_Router]
type=service
router=readconnroute
servers=server1
user=massi
passwd=massi
version_string=5.1-OLD-Bored-Mysql
filters=DuplicaFilter

[RW_Split]
type=service
router=readwritesplit
servers=server3,server2
user=massi
passwd=massi

[DuplicaFilter]
type=filter
module=tee
service=RW_Split

[RW_Listener]
type=listener
service=RW_Router
protocol=MySQLClient
port=4606


Accessing the RW_listener:

mysql -h 127.0.0.1 -P 4606 -umassi -pmassi



Debug version:

2014-12-11 08:48:48   Fatal: MaxScale received fatal signal 6. Attempting backtrace.
2014-12-11 08:48:48     ./maxscale() [0x53c80e]
2014-12-11 08:48:48     /lib64/libpthread.so.0(+0xf710) [0x7fd418a62710]
2014-12-11 08:48:48     /lib64/libc.so.6(gsignal+0x35) [0x7fd417318925]
2014-12-11 08:48:48     /lib64/libc.so.6(abort+0x175) [0x7fd41731a105]
2014-12-11 08:48:48     /lib64/libc.so.6(+0x2ba4e) [0x7fd417311a4e]
2014-12-11 08:48:48     /lib64/libc.so.6(__assert_perror_fail+0) [0x7fd417311b10]
2014-12-11 08:48:48     /usr/local/skysql/maxscale/modules/libreadwritesplit.so(+0x69ca) [0x7fd4142789ca]
2014-12-11 08:48:48     /usr/local/skysql/maxscale/modules/libtee.so(+0x3707) [0x7fd3fc2db707]
2014-12-11 08:48:48     /usr/local/skysql/maxscale/modules/libMySQLClient.so(+0x595d) [0x7fd3fe34b95d]
2014-12-11 08:48:48     ./maxscale() [0x54d3ec]
2014-12-11 08:48:48     ./maxscale(poll_waitevents+0x63d) [0x54ca8a]
2014-12-11 08:48:48     ./maxscale(main+0x1acc) [0x53f616]
2014-12-11 08:48:48     /lib64/libc.so.6(__libc_start_main+0xfd) [0x7fd417304d1d]
2014-12-11 08:48:48     ./maxscale() [0x53a92d]


Without debug:

we got mysql prompt but then maxscale is stucked
or
when don't have the prompt, it hangs after few welcome messages
Comment 1 Vilho Raatikka 2014-12-11 15:14:50 UTC
The assertion occurs because query is is not statement - but packet type. That is, it was sent to read connection router which doesn't examine MySQL packets except the header. Thus, the type of query is not set in mysql_client.c:gw_read_client_event:
>>>
                if (cap == 0 || (cap == RCAP_TYPE_PACKET_INPUT))
                {
                        stmt_input = false;
                }
                else if (cap == RCAP_TYPE_STMT_INPUT)
                {
                        stmt_input = true;

                        gwbuf_set_type(read_buffer, GWBUF_TYPE_MYSQL);
                }
>>>
Comment 2 Massimiliano 2014-12-11 16:00:52 UTC
Using readconnroute (with router_options=master) instead seems fine.

I found that "USE dbname" is not passed via tee filter:


4606 is the listener to a service with tee filter

root@maxscale-02 build]# mysql  -h 127.0.0.1 -P 4606 -u massi -pmassi



USE test; SELECT DATABASE()
client to maxscale:

T 127.0.0.1:40440 -> 127.0.0.1:4606 [AP]
  05 00 00 00 02 74 65 73    74                         .....test

T 127.0.0.1:4606 -> 127.0.0.1:40440 [AP]
  07 00 00 01 00 00 00 02    00 00 00                   ...........

T 127.0.0.1:40440 -> 127.0.0.1:4606 [AP]
  12 00 00 00 03 53 45 4c    45 43 54 20 44 41 54 41    .....SELECT DATA
  42 41 53 45 28 29                                     BASE()

T 127.0.0.1:4606 -> 127.0.0.1:40440 [AP]
  01 00 00 01 01 20 00 00    02 03 64 65 66 00 00 00    ..... ....def...
  0a 44 41 54 41 42 41 53    45 28 29 00 0c 08 00 22    .DATABASE()...."
  00 00 00 fd 00 00 1f 00    00 05 00 00 03 fe 00 00    ................
  02 00 05 00 00 04 04 74    65 73 74 05 00 00 05 fe    .......test.....
  00 00 02 00                                           ....

maxscale to backend:


T 127.0.0.1:56578 -> 127.0.0.1:3308 [AP]
  12 00 00 00 03 53 45 4c    45 43 54 20 44 41 54 41    .....SELECT DATA
  42 41 53 45 28 29                                     BASE()

T 127.0.0.1:3308 -> 127.0.0.1:56578 [AP]
  01 00 00 01 01 20 00 00    02 03 64 65 66 00 00 00    ..... ....def...
  0a 44 41 54 41 42 41 53    45 28 29 00 0c 08 00 22    .DATABASE()...."
  00 00 00 fd 00 00 1f 00    00 05 00 00 03 fe 00 00    ................
  02 00 01 00 00 04 fb 05    00 00 05 fe 00 00 02 00    ................


USE test was not sent



May be a similar issue is present with readwritesplit but I cannot test it
Comment 3 Vilho Raatikka 2014-12-11 16:35:46 UTC
(In reply to comment #2)
> Using readconnroute (with router_options=master) instead seems fine.

Using readconnroute _where_? in tee?
Comment 4 Vilho Raatikka 2014-12-12 08:27:41 UTC
gwbuf_type is not set and that is the immediate cause for assertion with Debug version.
Reason why the type is not set is in the way the packets are first processed in mysql_client.c client protocol module and then passed optionally to filters and router. There is a bug because it is assumed that when client protocol module reads incoming packet it can resolve which router will handle the packet processing. The code doesn't take into account that same packet can be processed by many different maxscales->routers[0], like in the case of readconnrouter->tee->readwritesplit.
Another problem is in readwritesplit where it is assumed that it is the first and the only router that will process tha data. So it includes checks that the buffer has correct type.

Required changes are:
- readwritesplit should check if buffer has no type and in that case, insted of asserting, merge incoming MySQL packet fragments into a single contiguous buffer.
- remove checks which enforce rules which are based on false assumption.
*/


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->maxscales->connect_maxscale(0);
    Test->try_query(Test->maxscales->conn_master[0], (char *) "show processlist");
    Test->try_query(Test->maxscales->conn_slave[0], (char *) "show processlist");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "show processlist");
    Test->maxscales->close_maxscale_connections(0);

    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
