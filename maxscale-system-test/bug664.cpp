/**
 * @file bug664.cpp bug664 regression case ("Core: Access of freed memory in gw_send_authentication_to_backend")
 *
 * - Maxscale.cnf contains:
 * @verbatim
[RW_Router]
type=service
router=readconnroute
servers=server1
user=maxuser
passwd=maxpwd
version_string=5.1-OLD-Bored-Mysql
filters=DuplicaFilter

[RW_Split]
type=service
router=readwritesplit
servers=server3,server2
user=maxuser
passwd=maxpwd

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

[Read Connection Router Slave]
type=service
router=readconnroute
router_options= slave
servers=server1,server2,server3,server4
user=maxuser
passwd=maxpwd
filters=QLA

[Read Connection Router Master]
type=service
router=readconnroute
router_options=master
servers=server1,server2,server3,server4
user=maxuser
passwd=maxpwd
filters=QLA

[Read Connection Listener Slave]
type=listener
service=Read Connection Router Slave
protocol=MySQLClient
port=4009

[Read Connection Listener Master]
type=listener
service=Read Connection Router Master
protocol=MySQLClient
port=4008

 @endverbatim
 * - warning is expected in the log, but not an error. All Maxscale services should be alive.
 * - Check MaxScale is alive
 */

/*
Vilho Raatikka 2014-12-29 18:12:23 UTC
All these cases are due to accessing freed dcb->data (MYSQL_session *):

==12419== Invalid read of size 1
==12419==    at 0x1B1434BA: gw_send_authentication_to_backend (mysql_common.c:544)
==12419==    by 0x1B13F90E: gw_read_backend_event (mysql_backend.c:228)
==12419==    by 0x588CA2: process_pollq (poll.c:858)
==12419==    by 0x58854B: poll_waitevents (poll.c:608)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==  Address 0x18690285 is 149 bytes inside a block of size 278 free'd
==12419==    at 0x4C2AF6C: free (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x57D806: dcb_final_free (dcb.c:406)
==12419==    by 0x57DDE6: dcb_process_zombies (dcb.c:603)
==12419==    by 0x588598: poll_waitevents (poll.c:613)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==
==12419== Invalid read of size 1
==12419==    at 0x1B1434D6: gw_send_authentication_to_backend (mysql_common.c:547)
==12419==    by 0x1B13F90E: gw_read_backend_event (mysql_backend.c:228)
==12419==    by 0x588CA2: process_pollq (poll.c:858)
==12419==    by 0x58854B: poll_waitevents (poll.c:608)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==  Address 0x186901f0 is 0 bytes inside a block of size 278 free'd
==12419==    at 0x4C2AF6C: free (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x57D806: dcb_final_free (dcb.c:406)
==12419==    by 0x57DDE6: dcb_process_zombies (dcb.c:603)
==12419==    by 0x588598: poll_waitevents (poll.c:613)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==
==12419== Invalid read of size 8
==12419==    at 0x1B1435FC: gw_send_authentication_to_backend (mysql_common.c:572)
==12419==    by 0x1B13F90E: gw_read_backend_event (mysql_backend.c:228)
==12419==    by 0x588CA2: process_pollq (poll.c:858)
==12419==    by 0x58854B: poll_waitevents (poll.c:608)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==  Address 0x186901f0 is 0 bytes inside a block of size 278 free'd
==12419==    at 0x4C2AF6C: free (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x57D806: dcb_final_free (dcb.c:406)
==12419==    by 0x57DDE6: dcb_process_zombies (dcb.c:603)
==12419==    by 0x588598: poll_waitevents (poll.c:613)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==
==12419== Invalid read of size 8
==12419==    at 0x1B143606: gw_send_authentication_to_backend (mysql_common.c:572)
==12419==    by 0x1B13F90E: gw_read_backend_event (mysql_backend.c:228)
==12419==    by 0x588CA2: process_pollq (poll.c:858)
==12419==    by 0x58854B: poll_waitevents (poll.c:608)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==  Address 0x186901f8 is 8 bytes inside a block of size 278 free'd
==12419==    at 0x4C2AF6C: free (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x57D806: dcb_final_free (dcb.c:406)
==12419==    by 0x57DDE6: dcb_process_zombies (dcb.c:603)
==12419==    by 0x588598: poll_waitevents (poll.c:613)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==
==12419== Invalid read of size 4
==12419==    at 0x1B143611: gw_send_authentication_to_backend (mysql_common.c:572)
==12419==    by 0x1B13F90E: gw_read_backend_event (mysql_backend.c:228)
==12419==    by 0x588CA2: process_pollq (poll.c:858)
==12419==    by 0x58854B: poll_waitevents (poll.c:608)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==  Address 0x18690200 is 16 bytes inside a block of size 278 free'd
==12419==    at 0x4C2AF6C: free (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x57D806: dcb_final_free (dcb.c:406)
==12419==    by 0x57DDE6: dcb_process_zombies (dcb.c:603)
==12419==    by 0x588598: poll_waitevents (poll.c:613)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==
==12419== Invalid read of size 1
==12419==    at 0x4C2CCA2: strlen (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x1B143719: gw_send_authentication_to_backend (mysql_common.c:604)
==12419==    by 0x1B13F90E: gw_read_backend_event (mysql_backend.c:228)
==12419==    by 0x588CA2: process_pollq (poll.c:858)
==12419==    by 0x58854B: poll_waitevents (poll.c:608)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==  Address 0x18690204 is 20 bytes inside a block of size 278 free'd
==12419==    at 0x4C2AF6C: free (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x57D806: dcb_final_free (dcb.c:406)
==12419==    by 0x57DDE6: dcb_process_zombies (dcb.c:603)
==12419==    by 0x588598: poll_waitevents (poll.c:613)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==
==12419== Invalid read of size 1
==12419==    at 0x4C2CCB4: strlen (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x1B143719: gw_send_authentication_to_backend (mysql_common.c:604)
==12419==    by 0x1B13F90E: gw_read_backend_event (mysql_backend.c:228)
==12419==    by 0x588CA2: process_pollq (poll.c:858)
==12419==    by 0x58854B: poll_waitevents (poll.c:608)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==  Address 0x18690205 is 21 bytes inside a block of size 278 free'd
==12419==    at 0x4C2AF6C: free (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x57D806: dcb_final_free (dcb.c:406)
==12419==    by 0x57DDE6: dcb_process_zombies (dcb.c:603)
==12419==    by 0x588598: poll_waitevents (poll.c:613)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==
==12419== Invalid read of size 1
==12419==    at 0x4C2CCA2: strlen (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x1B143893: gw_send_authentication_to_backend (mysql_common.c:660)
==12419==    by 0x1B13F90E: gw_read_backend_event (mysql_backend.c:228)
==12419==    by 0x588CA2: process_pollq (poll.c:858)
==12419==    by 0x58854B: poll_waitevents (poll.c:608)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==  Address 0x18690204 is 20 bytes inside a block of size 278 free'd
==12419==    at 0x4C2AF6C: free (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x57D806: dcb_final_free (dcb.c:406)
==12419==    by 0x57DDE6: dcb_process_zombies (dcb.c:603)
==12419==    by 0x588598: poll_waitevents (poll.c:613)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==
==12419== Invalid read of size 1
==12419==    at 0x4C2CCB4: strlen (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x1B143893: gw_send_authentication_to_backend (mysql_common.c:660)
==12419==    by 0x1B13F90E: gw_read_backend_event (mysql_backend.c:228)
==12419==    by 0x588CA2: process_pollq (poll.c:858)
==12419==    by 0x58854B: poll_waitevents (poll.c:608)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==  Address 0x18690205 is 21 bytes inside a block of size 278 free'd
==12419==    at 0x4C2AF6C: free (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x57D806: dcb_final_free (dcb.c:406)
==12419==    by 0x57DDE6: dcb_process_zombies (dcb.c:603)
==12419==    by 0x588598: poll_waitevents (poll.c:613)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==
==12419== Invalid read of size 1
==12419==    at 0x4C2DE21: memcpy@@GLIBC_2.14 (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x1B1438AF: gw_send_authentication_to_backend (mysql_common.c:660)
==12419==    by 0x1B13F90E: gw_read_backend_event (mysql_backend.c:228)
==12419==    by 0x588CA2: process_pollq (poll.c:858)
==12419==    by 0x58854B: poll_waitevents (poll.c:608)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==  Address 0x1869020a is 26 bytes inside a block of size 278 free'd
==12419==    at 0x4C2AF6C: free (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x57D806: dcb_final_free (dcb.c:406)
==12419==    by 0x57DDE6: dcb_process_zombies (dcb.c:603)
==12419==    by 0x588598: poll_waitevents (poll.c:613)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==
==12419== Invalid read of size 2
==12419==    at 0x4C2DEA0: memcpy@@GLIBC_2.14 (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x1B1438AF: gw_send_authentication_to_backend (mysql_common.c:660)
==12419==    by 0x1B13F90E: gw_read_backend_event (mysql_backend.c:228)
==12419==    by 0x588CA2: process_pollq (poll.c:858)
==12419==    by 0x58854B: poll_waitevents (poll.c:608)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==  Address 0x18690206 is 22 bytes inside a block of size 278 free'd
==12419==    at 0x4C2AF6C: free (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x57D806: dcb_final_free (dcb.c:406)
==12419==    by 0x57DDE6: dcb_process_zombies (dcb.c:603)
==12419==    by 0x588598: poll_waitevents (poll.c:613)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==
==12419== Invalid read of size 1
==12419==    at 0x4C2CCA2: strlen (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x1B1438BE: gw_send_authentication_to_backend (mysql_common.c:661)
==12419==    by 0x1B13F90E: gw_read_backend_event (mysql_backend.c:228)
==12419==    by 0x588CA2: process_pollq (poll.c:858)
==12419==    by 0x58854B: poll_waitevents (poll.c:608)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==  Address 0x18690204 is 20 bytes inside a block of size 278 free'd
==12419==    at 0x4C2AF6C: free (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x57D806: dcb_final_free (dcb.c:406)
==12419==    by 0x57DDE6: dcb_process_zombies (dcb.c:603)
==12419==    by 0x588598: poll_waitevents (poll.c:613)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==
==12419== Invalid read of size 1
==12419==    at 0x4C2CCB4: strlen (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x1B1438BE: gw_send_authentication_to_backend (mysql_common.c:661)
==12419==    by 0x1B13F90E: gw_read_backend_event (mysql_backend.c:228)
==12419==    by 0x588CA2: process_pollq (poll.c:858)
==12419==    by 0x58854B: poll_waitevents (poll.c:608)
==12419==    by 0x57C11B: main (gateway.c:1792)
==12419==  Address 0x18690205 is 21 bytes inside a block of size 278 free'd
==12419==    at 0x4C2AF6C: free (in /usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so)
==12419==    by 0x57D806: dcb_final_free (dcb.c:406)
==12419==    by 0x57DDE6: dcb_process_zombies (dcb.c:603)
==12419==    by 0x588598: poll_waitevents (poll.c:613)
==12419==    by 0x57C11B: main (gateway.c:1792)
Comment 1 Vilho Raatikka 2014-12-29 18:29:11 UTC
        dcb_final_free:don't free dcb->data, it is either freed in session_alloc if session creation fails or in session_free only.
    mysql_client.c:gw_mysql_do_authentication:if anything fails, and session_alloc won't be called, free dcb->data.
    mysql_common.c:gw_send_authentication_to_backend:if session is already closing then return with error.
Comment 2 Markus Mäkelä 2014-12-30 08:58:59 UTC
Created attachment 170 [details]
failing configuration

The attached configuration currently crashes into a debug assert in handleError in readconnroute.c when connecting to port 4006. If this is removed, the next point of failure is in dcb_final_free when the session->data object is freed.
Comment 3 Vilho Raatikka 2014-12-30 10:14:43 UTC
(In reply to comment #2)
> Created attachment 170 [details]
> failing configuration
>
> The attached configuration currently crashes into a debug assert in
> handleError in readconnroute.c when connecting to port 4006. If this is
> removed, the next point of failure is in dcb_final_free when the
> session->data object is freed.

Should this be open or closed based on the information provided?
Comment 4 Markus Mäkelä 2014-12-30 10:17:55 UTC
My apologies, I thought I did reopen it.
Comment 5 Vilho Raatikka 2014-12-30 10:27:32 UTC
Fixed double freeing dcb->data if authentication phase fails.
Comment 6 Vilho Raatikka 2014-12-30 10:30:12 UTC
Reopen due to crash. Another double free somewhere.
Comment 7 Vilho Raatikka 2014-12-30 11:36:38 UTC
Cloned session was freeing the shared 'data' dcb->data/session->data. Now only session_free for the non-clone session is allowed to free the data.

*/



#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    Test->connect_maxscale();

    Test->tprintf("Trying query to ReadConn master\n");
    fflush(stdout);
    Test->try_query(Test->conn_master, "show processlist;");
    Test->tprintf("Trying query to ReadConn slave\n");
    Test->try_query(Test->conn_slave, "show processlist;");

    Test->close_maxscale_connections();

    Test->check_log_err((char *) "Creating client session for Tee filter failed. Terminating session.", true);
    Test->check_log_err((char *) "Failed to create filter 'DuplicaFilter' for service 'RW_Router'", true);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

