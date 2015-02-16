/**
 * @file slave_failover.cpp  Check how Maxscale works in case of one slave failure
 *
 * - Connect to RWSplit
 * - find which backend slave is used for connection
 * - blocm mariadb on the slave with firewall
 * - wait 60 seconds
 * - check which slave is used for connection now, expecting any other slave
 * - check warning in the error log about broken slave
 * - unblock mariadb backend (restore slave firewall settings)
 * - check if Maxscale still alive
 */

#include <my_config.h>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
    int global_result = 0;
    char sys1[4096];

    unsigned int current_slave;
    unsigned int old_slave;

    Test->ReadEnv();
    Test->PrintIP();

    printf("Connecting to RWSplit %s\n", Test->Maxscale_IP);
    Test->ConnectRWSplit();

    // this is the same test, but with killing VM
/*    printf("Checking current slave\n");
    old_slave = FindConnectedSlave(Test, &global_result);

    printf("Killing VM\n"); fflush(stdout);
    sprintf(&sys1[0], "%s %s", Test->KillVMCommand, Test->repl->IP[old_slave]);
    system(sys1);
    printf("Sleeping 60 seconds to let MaxScale to find new slave\n");fflush(stdout);
    sleep(60);

    current_slave = FindConnectedSlave(Test, &global_result);
    if ((current_slave == old_slave) || (current_slave < 0)) {printf("FAILED: No failover happened\n"); global_result=1;}


    char err1[1024];

    sprintf(&err1[0], "Error : Monitor was unable to connect to server %s", Test->repl->IP[old_slave]);
    CheckLogErr(err1 , TRUE);

    printf("Starting VM back\n"); fflush(stdout);
    sprintf(&sys1[0], "%s %s", Test->StartVMCommand, Test->repl->IP[old_slave]);
    system(sys1);

    printf("Sleeping 60 seconds to let VM start\n"); fflush(stdout);
    sleep(60);

    printf("Doing test again, but with firewall block instead of VM killing\n");*/

    printf("Checking current slave\n"); fflush(stdout);
    old_slave = FindConnectedSlave(Test, &global_result);

    printf("Setup firewall to block mysql on old slave (oldslave is node %d)\n", old_slave); fflush(stdout);
    if ((old_slave < 0) || (old_slave >= Test->repl->N)) {
        printf("Active slave is not found\n");
        global_result++;
    } else {
        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"iptables -I INPUT -p tcp --dport %d -j REJECT\"", Test->repl->sshkey[old_slave], Test->repl->IP[old_slave], Test->repl->Ports[old_slave]);
        printf("%s\n", sys1); fflush(stdout);
        system(sys1);

        printf("Sleeping 60 seconds to let MaxScale to find new slave\n"); fflush(stdout);
        sleep(60);

        current_slave = FindConnectedSlave(Test, &global_result);
        if ((current_slave == old_slave) || (current_slave < 0)) {printf("FAILED: No failover happened\n"); global_result=1;}

        printf("Setup firewall back to allow mysql\n"); fflush(stdout);
        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"iptables -I INPUT -p tcp --dport %d -j ACCEPT\"", Test->repl->sshkey[old_slave], Test->repl->IP[old_slave], Test->repl->Ports[old_slave]);
        printf("%s\n", sys1);  fflush(stdout);
        system(sys1);

        global_result += CheckMaxscaleAlive();

        Test->CloseRWSplit();
    }

    Test->repl->StartReplication();


    exit(global_result);
}
