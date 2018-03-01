/**
 * @file mxs501_tee_usedb.cpp mxs501 regression case ("USE <db> hangs when Tee filter uses matching")
 * @verbatim
[RW Split Router]
type=service
router= readwritesplit
servers=server1,     server2,              server3,server4
user=skysql
passwd=skysql
router_options=slave_selection_criteria=LEAST_ROUTER_CONNECTIONS
max_slave_connections=1
filters=QLA,duplicate

[duplicate]
type=filter
module=tee
match=insert
service=Connection Router Master


[Read Connection Router Slave]
type=service
router=readconnroute
router_options= slave
servers=server1,server2,server3,server4
user=skysql
passwd=skysql
filters=QLA

[Read Connection Router Master]
type=service
router=readconnroute
router_options=master
servers=server1,server2,server3,server4
user=skysql
passwd=skysql
filters=QLA
@endverbatim
 *
 * try USE test command against all routers
 */

#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->connect_maxscale();

    Test->set_timeout(10);
    Test->tprintf("Trying USE db against RWSplit\n");
    Test->try_query(Test->conn_rwsplit, (char *) "USE mysql");
    Test->try_query(Test->conn_rwsplit, (char *) "USE test");
    Test->set_timeout(10);
    Test->tprintf("Trying USE db against ReadConn master\n");
    Test->try_query(Test->conn_master, (char *) "USE mysql");
    Test->try_query(Test->conn_master, (char *) "USE test");
    Test->set_timeout(10);
    Test->tprintf("Trying USE db against ReadConn slave\n");
    Test->try_query(Test->conn_master, (char *) "USE mysql");
    Test->try_query(Test->conn_slave, (char *) "USE test");

    Test->set_timeout(10);
    Test->close_maxscale_connections();

    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
