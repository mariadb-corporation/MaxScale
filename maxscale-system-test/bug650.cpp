/**
 * @file bug650.cpp  regression case for bug 650 ("Hints, RWSplit: MaxScale goes into infinite loop and crashes") and bug645
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

 @endverbatim
 * - try to connect
 * - try simple query using ReadConn router (both, master and slave)
 * - check errors in the log
 @verbatim
    Couldn't find suitable Master from 2 candidates
    Failed to create RW_Split session.
    Creating client session for Tee filter failed. Terminating session.
    Failed to create filter 'DuplicaFilter' for service 'RW_Router'
    Setting up filters failed. Terminating session RW_Router
 @endverbatim
 */


#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    Test->connect_maxscale();
    Test->tprintf("Trying query to ReadConn master\n");
    Test->try_query(Test->conn_master, (char *) "show processlist");
    Test->tprintf("Trying query to ReadConn slave\n");
    Test->try_query(Test->conn_slave, (char *) "show processlist");
    Test->tprintf("Trying query to RWSplit, expecting failure\n");
    if (execute_query(Test->conn_rwsplit, (char *) "show processlist") == 0)
    {
        Test->add_result(1, "Query is ok, but failure is expected\n");
    }
    Test->close_maxscale_connections();

    Test->tprintf("Checking logs\n");

    Test->check_log_err((char *) "Couldn't find suitable Master from 2 candidates", true);
    Test->check_log_err((char *) "Failed to create new router session for service 'RW_Split'", true);
    Test->check_log_err((char *) "Creating client session for Tee filter failed. Terminating session.", true);
    Test->check_log_err((char *) "Failed to create filter 'DuplicaFilter' for service 'RW_Router'", true);
    Test->check_log_err((char *) "Setting up filters failed. Terminating session RW_Router", true);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

