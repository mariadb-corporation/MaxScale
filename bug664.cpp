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


#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{

    TestConnections * Test = new TestConnections();
    int global_result = 0;
    //int i;
    //char sys1[4096];

    Test->ReadEnv();
    Test->PrintIP();
    //CheckLogErr((char *) "Warning : Unsupported router option \"slave\"", TRUE);
    //global_result    += CheckLogErr((char *) "Error : Couldn't find suitable Master", FALSE);
    Test->ConnectMaxscale();

    printf("Trying query to ReadConn master\n"); fflush(stdout);
    global_result += execute_query(Test->conn_master, "show processlist;");
    printf("Trying query to ReadConn slave\n"); fflush(stdout);
    global_result += execute_query(Test->conn_slave, "show processlist;");

    Test->CloseMaxscaleConn();

    global_result    += CheckLogErr((char *) "Error : Creating client session for Tee filter failed. Terminating session.", TRUE);
    global_result    += CheckLogErr((char *) "Error : Failed to create filter 'DuplicaFilter' for service 'RW_Router'", TRUE);

    return(global_result);
}

