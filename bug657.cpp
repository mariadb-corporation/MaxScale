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

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

//pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
//int exit_flag = 0;

TestConnections * Test ;

//char sql[1000000];

//void *parall_traffic( void *ptr );

int main(int argc, char *argv[])
{
    //pthread_t parall_traffic1[100];
    //int check_iret[100];

    Test = new TestConnections(argv[0]);
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    printf("Connecting to ReadConn Master %s\n", Test->Maxscale_IP);
    Test->ConnectReadMaster();

    char sys1[4096];
    sleep(1);

    printf("Setup firewall to block mysql on master\n"); fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"iptables -I INPUT -p tcp --dport %d -j REJECT\"", Test->repl->sshkey[0], Test->repl->IP[0], Test->repl->Ports[0]);
    printf("%s\n", sys1); fflush(stdout);
    system(sys1); fflush(stdout);

    sleep(10);

    printf("Reconnecting to ReadConnMaster\n"); fflush(stdout);
    Test->CloseReadMaster();
    Test->ConnectReadMaster();

    //printf("Trying query to RWSplit, expecting failure, but not a crash\n"); fflush(stdout);
    //execute_query(Test->conn_rwsplit, (char *) "show processlist;");fflush(stdout);

    sleep(10);


    printf("Setup firewall back to allow mysql\n"); fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"iptables -I INPUT -p tcp --dport %d -j ACCEPT\"", Test->repl->sshkey[0], Test->repl->IP[0], Test->repl->Ports[0]);
    printf("%s\n", sys1);  fflush(stdout);
    system(sys1); fflush(stdout);
    sleep(10);

    printf("Closing connection\n"); fflush(stdout);

    Test->CloseReadMaster(); fflush(stdout);

    printf("Checking Maxscale is alive\n"); fflush(stdout);

    global_result += CheckMaxscaleAlive(); fflush(stdout);


    //exit_flag = 1;
    //sleep(10);

    Test->Copy_all_logs(); return(global_result);
}

/*
void *parall_traffic( void *ptr )
{
    MYSQL * conn;
    while (exit_flag == 0) {
        conn = Test->OpenRWSplitConn();
        execute_query(conn, sql);
        mysql_close(conn);
        fflush(stdout);
    }
    return NULL;
}

*/
