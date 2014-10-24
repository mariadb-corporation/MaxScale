#include <my_config.h>
#include "testconnections.h"

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;

    unsigned int current_slave;
    unsigned int old_slave;

    Test->ReadEnv();
    Test->PrintIP();

    printf("Connecting to RWSplit %s\n", Test->Maxscale_IP);
    Test->ConnectRWSplit();

    printf("Checking current slave\n");
    old_slave = FindConnectedSlave(Test, &global_result);

    char sys1[4096];
    printf("Killing VM\n"); fflush(stdout);
    sprintf(&sys1[0], "%s %s", Test->KillVMCommand, Test->repl->IP[old_slave]);
    system(sys1);
    printf("Sleeping 60 seconds to let MaxScale to find new slave\n");
    sleep(60);

    current_slave = FindConnectedSlave(Test, &global_result);
    if ((current_slave == old_slave) || (current_slave < 0)) {printf("FAILED: No failover happened\n"); global_result=1;}

    Test->CloseRWSplit();

    char err1[1024];

    sprintf(&err1[0], "Error : Monitor was unable to connect to server %s", Test->repl->IP[old_slave]);
    CheckLogErr(err1 , TRUE);

    printf("Starting VM back\n"); fflush(stdout);
    sprintf(&sys1[0], "%s %s", Test->StartVMCommand, Test->repl->IP[old_slave]);
    system(sys1);

    printf("Sleeping 60 seconds to let VM start\n");
    sleep(60);

    printf("Doing test agin but with firewall block instead of VM killing\n");

    printf("Checking current slave\n");
    old_slave = FindConnectedSlave(Test, &global_result);

    printf("Setup firewall to block mysql\n"); fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"iptables -I INPUT -p tcp --dport %d -j REJECT\"", Test->repl->sshkey[old_slave], Test->repl->IP[old_slave], Test->repl->Ports[old_slave]);
    printf("%s\n", sys1);
    system(sys1);

    printf("Sleeping 60 seconds to let MaxScale to find new slave\n");
    sleep(60);

    current_slave = FindConnectedSlave(Test, &global_result);
    if ((current_slave == old_slave) || (current_slave < 0)) {printf("FAILED: No failover happened\n"); global_result=1;}

    printf("Setup firewall back to allow mysql\n"); fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s \"iptables -I INPUT -p tcp --dport %d -j ACCEPT\"", Test->repl->sshkey[old_slave], Test->repl->IP[old_slave], Test->repl->Ports[old_slave]);
    printf("%s\n", sys1);
    system(sys1);

    CheckMaxscaleAlive();

    exit(global_result);
}
