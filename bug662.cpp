/**
 * @file bug662.cpp regression case for bug 662 ("")
 *
 * - block all Mariadb servers  Firewall
 * - restart MaxScale
 * - check it took no more then 20 seconds
 * - unblock Mariadb servers
 * - sleep one minute
 * - check if Maxscale is alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;
    int i;

    Test->ReadEnv();
    Test->PrintIP();

    printf("Connecting to Maxscale %s\n", Test->Maxscale_IP);

    char sys1[4096];

    printf("Connecting to Maxscale %s to check its behaviour in case of blocking all bacxkends\n", Test->Maxscale_IP);
    Test->ConnectMaxscale();

    for (i = 0; i < Test->repl->N; i++) {
        printf("Setup firewall to block mysql on node %d\n", i); fflush(stdout);
        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"iptables -I INPUT -p tcp --dport %d -j REJECT\"", Test->repl->sshkey[i], Test->repl->IP[i], Test->repl->Ports[i]);
        printf("%s\n", sys1); fflush(stdout);
        system(sys1); fflush(stdout);
    }

    time_t now, later;
    double seconds;

    time(&now);

    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"service maxscale restart\"", Test->Maxscale_sshkey, Test->Maxscale_IP);
    printf("%s\n", sys1); fflush(stdout);
    system(sys1); fflush(stdout);


    time(&later);
    seconds = difftime(later, now);

    printf("restart took %.f seconds\n", seconds);

    if (seconds > 20 ) {
        printf("Maxscale restart took more 20 seconds, Test FAILED\n");
        global_result++;
    }


    for (i = 0; i < Test->repl->N; i++) {
        printf("Setup firewall back to allow mysql on node %d\n", i); fflush(stdout);
        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"iptables -I INPUT -p tcp --dport %d -j ACCEPT\"", Test->repl->sshkey[i], Test->repl->IP[i], Test->repl->Ports[i]);
        printf("%s\n", sys1);  fflush(stdout);
        system(sys1); fflush(stdout);
    }

    printf("Sleeping 60 seconds\n"); fflush(stdout);
    sleep(60);

    printf("Checking Maxscale is alive\n"); fflush(stdout);
    global_result += CheckMaxscaleAlive(); fflush(stdout);
    if (global_result !=0) {
        printf("MaxScale is not alive\n");
    } else {
        printf("MaxScale is still alive\n");
    }

    Test->CloseMaxscaleConn(); fflush(stdout);

    printf("Reconnecting and trying query to RWSplit\n"); fflush(stdout);
    Test->ConnectMaxscale();
    global_result += execute_query(Test->conn_rwsplit, (char *) "show processlist;");
    printf("Trying query to ReadConn master\n"); fflush(stdout);
    global_result += execute_query(Test->conn_master, (char *) "show processlist;");
    printf("Trying query to ReadConn slave\n"); fflush(stdout);
    global_result += execute_query(Test->conn_slave, (char *) "show processlist;");
    Test->CloseMaxscaleConn();

    exit(global_result);
}
