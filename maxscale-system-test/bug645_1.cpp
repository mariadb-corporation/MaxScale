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
servers=server3,server2
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
 * - try to connect to all services except 4016
 * - try simple query
 * - check ReadConn is ok
 * - check log for presens of "Couldn't find suitable Master from 2 candidates" errors
 */


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->connect_maxscale();
    Test->tprintf("trying query to RWSplit, expecting failure\n");
    if (execute_query(Test->conn_rwsplit, (char *) "show processlist") == 0)
    {
        Test->add_result(1, "Query is ok, but failue is expected\n");
    }
    Test->tprintf("Trying query to ReadConn router master\n");
    Test->try_query(Test->conn_master, (char *) "show processlist");
    Test->tprintf("Trying query to ReadConn router slave\n");
    Test->try_query(Test->conn_slave, (char *) "show processlist");

    Test->close_maxscale_connections();

    Test->check_log_err((char *) "Couldn't find suitable Master from 2 candidates", true);
    Test->check_log_err((char *) "Creating client session for Tee filter failed. Terminating session.", true);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
