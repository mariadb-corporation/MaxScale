/**
 * @file bug657.cpp regression case for bug 657 ("Tee filter: closing child session causes MaxScale to fail")
 *
 * - Configure readconnrouter with tee filter and tee filter with a readwritesplit as a child service.
 * @verbatim
[RW Split Router]
type=service
router= readwritesplit
servers=server1,     server2,              server3,server4
user=skysql
passwd=skysql
#filters=QLA

[Read Connection Router Slave]
type=service
router=readconnroute
router_options= slave
servers=server1,server2,server3,server4
user=skysql
passwd=skysql
filters=TEE

[Read Connection Router Master]
type=service
router=readconnroute
router_options=master
servers=server1,server2,server3,server4
user=skysql
passwd=skysql
filters=TEE

[TEE]
type=filter
module=tee
service=RW Split Router
@endverbatim
 * - Start MaxScale
 * - Connect readconnrouter
 * - Fail the master node
 * - Reconnect readconnrouter
 */

/*
Vilho Raatikka 2014-12-22 08:35:52 UTC
How to reproduce:
1. Configure readconnrouter with tee filter and tee filter with a readwritesplit as a child service.
2. Start MaxScale
3. Connect readconnrouter
4. Fail the master node
5. Reconnect readconnrouter

As a consequence, next routeQuery will be duplicated to closed readwritesplit router and eventually fred memory will be accessed which causes SEGFAULT.

Reason for this is that situation where child (=branch -) session is closed as a consequence of node failure, is not handled in tee filter. Tee filter handles the case where client closes the session.
Comment 1 Vilho Raatikka 2014-12-22 09:14:13 UTC
Background: client session may be closed for different reasons. If client actively closes it by sending COM_QUIT packet, it happens from top to bottom: packet is identified and client DCB is closed. Client's DCB close routine also closes the client router session.

If backend fails and monitor detects it, then every DCB that isn't running or isn't master, slave, joined (Galera) nor ndb calls its hangup function. If the failed node was master then client session gets state SESSION_STATE_STOPPING which triggers first closing the client DCB and as a part of it, the whole session.

In tee filter, the first issue is the client DCB's close routine which doesn't trigger closing the session. The other issue is that if child session gets closed there's no mechanism that would prevent future queries being routed to tee's child service. As a consequence, future calls to routeQuery will access closed child session including freed memory etc.
Comment 2 Vilho Raatikka 2014-12-22 22:32:25 UTC
session.c:session_free:if session is child of another service (tee in this case), it is the parent which releases child's allocated memory back to the system. This now also includes the child router session.
    dcb.h: Added DCB_IS_CLONE macro
    tee.c:freeSession:if parent session triggered closing of tee, then child session may not be closed yet. In that case free the child session first and only then free child router session and release child session's memory back to system.
    tee.c:routeQuery: only route if child session is ready for routing. Log if session is not ready for routing and set tee session inactive
    mysql_client.c:gw_client_close:if DCB is cloned one don't close the protocol because they it is shared with the original DCB.
Comment 3 Vilho Raatikka 2014-12-23 10:04:11 UTC
If monitor haven't yet changed the status for failed backend, even the fixed won't notice the failure, and the client is left waiting for reply until some lower level timeout exceeds and closes the socket.

The solution is to register a callback function to readconnrouter's backend DCB in the same way that it is done in readwritesplit. Callback needs to be implemented and tests added.
By using this mechanism the client must wait at most one monitor interval before the session is closed.

Vilho Raatikka 2014-12-31 23:19:41 UTC
filter.c:filter_free:if filter parameter is NULL, return.
    tee.c:freeSession: if my_session->dummy_filterdef is NULL, don't try to release the memory
*/


#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

int main(int argc, char *argv[])
{
    TestConnections *Test = new TestConnections(argc, argv);
    Test->set_timeout(200);

    Test->tprintf("Connecting to ReadConn Master %s\n", Test->maxscale_IP);
    Test->connect_readconn_master();

    sleep(1);

    Test->tprintf("Setup firewall to block mysql on master\n");
    Test->repl->block_node(0);

    sleep(10);

    Test->tprintf("Reconnecting to ReadConnMaster\n");
    Test->close_readconn_master();
    Test->connect_readconn_master();

    sleep(5);

    Test->repl->unblock_node(0);
    sleep(10);

    Test->tprintf("Closing connection\n");

    Test->close_readconn_master();
    fflush(stdout);

    Test->tprintf("Checking Maxscale is alive\n");

    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
