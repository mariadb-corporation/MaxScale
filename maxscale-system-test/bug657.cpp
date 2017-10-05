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

#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

int main(int argc, char *argv[])
{
    TestConnections *Test = new TestConnections(argc, argv);
    Test->set_timeout(200);

    Test->tprintf("Connecting to ReadConn Master %s\n", Test->maxscales->IP[0]);
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
