/**
 * @file kill_master.cpp Checks Maxscale behaviour in case if Master node is blocked
 *
 * - Connecto RWSplit
 * - block Mariadb server on Master node by Firewall
 * - try simple query *show processlist" expecting failure, but not a crash
 * - check if Maxscale is alive
 * - reconnect and check if query execution is ok
 */

#include <my_config.h>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    printf("Connecting to RWSplit %s\n", Test->Maxscale_IP);
    Test->ConnectRWSplit();

    char sys1[4096];

    printf("Setup firewall to block mysql on master\n"); fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"iptables -I INPUT -p tcp --dport %d -j REJECT\"", Test->repl->sshkey[0], Test->repl->IP[0], Test->repl->Ports[0]);
    printf("%s\n", sys1); fflush(stdout);
    system(sys1);

    printf("Trying query to RWSplit, expecting failure, but not a crash\n"); fflush(stdout);
    execute_query(Test->conn_rwsplit, (char *) "show processlist;");


    printf("Setup firewall back to allow mysql\n"); fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"iptables -I INPUT -p tcp --dport %d -j ACCEPT\"", Test->repl->sshkey[0], Test->repl->IP[0], Test->repl->Ports[0]);
    printf("%s\n", sys1);  fflush(stdout);
    system(sys1);
    sleep(10);

    global_result += CheckMaxscaleAlive();

    Test->CloseRWSplit();


    printf("Reconnecting and trying query to RWSplit\n"); fflush(stdout);
    Test->ConnectRWSplit();
    global_result += execute_query(Test->conn_rwsplit, (char *) "show processlist;");
    Test->CloseRWSplit();

    exit(global_result);
}

