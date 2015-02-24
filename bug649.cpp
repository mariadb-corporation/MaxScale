/**
 * @file bug649.cpp regression case for bug 649 ("Segfault using RW Splitter")
 *
 * - Connect to RWSplit
 * - create load on RWSplit (25 threads doing long INSERTs in the loop)
 * - block Mariadb server on Master node by Firewall
 * - unblock Mariadb server
 * - check if Maxscale is alive
 * - reconnect and check if query execution is ok
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int exit_flag = 0;

TestConnections * Test ;

char sql[1000000];

void *parall_traffic( void *ptr );

int main(int argc, char *argv[])
{
    pthread_t parall_traffic1[100];
    int check_iret[100];

    Test = new TestConnections(argv[0]);
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    printf("Connecting to RWSplit %s\n", Test->Maxscale_IP);
    Test->ConnectRWSplit();

    global_result += create_t1(Test->conn_rwsplit);
    create_insert_string(sql, 65000, 1);


    for (int j = 0; j < 25; j++) {
        check_iret[j] = pthread_create( &parall_traffic1[j], NULL, parall_traffic, NULL);
    }

    char sys1[4096];
    sleep(1);

    printf("Setup firewall to block mysql on master\n"); fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"iptables -I INPUT -p tcp --dport %d -j REJECT\"", Test->repl->sshkey[0], Test->repl->IP[0], Test->repl->Ports[0]);
    printf("%s\n", sys1); fflush(stdout);
    system(sys1); fflush(stdout);

    sleep(1);

    printf("Trying query to RWSplit, expecting failure, but not a crash\n"); fflush(stdout);
    execute_query(Test->conn_rwsplit, (char *) "show processlist;");fflush(stdout);

    sleep(1);


    printf("Setup firewall back to allow mysql\n"); fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"iptables -I INPUT -p tcp --dport %d -j ACCEPT\"", Test->repl->sshkey[0], Test->repl->IP[0], Test->repl->Ports[0]);
    printf("%s\n", sys1);  fflush(stdout);
    system(sys1); fflush(stdout);
    sleep(10);

    printf("Checking Maxscale is alive\n"); fflush(stdout);
    global_result += CheckMaxscaleAlive(); fflush(stdout);

    Test->CloseRWSplit(); fflush(stdout);


    printf("Reconnecting and trying query to RWSplit\n"); fflush(stdout);
    Test->ConnectRWSplit();
    global_result += execute_query(Test->conn_rwsplit, (char *) "show processlist;");
    Test->CloseRWSplit();

    exit_flag = 1;
    sleep(10);

    Test->Copy_all_logs(); return(global_result);
}


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
