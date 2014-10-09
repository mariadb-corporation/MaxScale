#include "testconnections.h"

int FindConnectedSlave(TestConnections* Test, int * global_result)
{
    int conn_num;
    int all_conn;
    int current_slave = -1;
    Test->repl->Connect();
    for (int i = 0; i < Test->repl->N; i++) {
        conn_num = get_conn_num(Test->repl->nodes[i], Test->Maxscale_IP, (char *) "test");
        printf("connections to %d: %u\n", i, conn_num);
        if ((i == 0) && (conn_num != 1)) {printf("There is no connection to master\n"); *global_result = 1;}
        all_conn += conn_num;
        if ((i != 0) && (conn_num != 0)) {current_slave = i;}
    }
    if (all_conn != 2) {printf("total number of connections is not 2, it is %d\n", all_conn); *global_result = 1;}
    printf("Now connected slave node is %d (%s)\n", current_slave, Test->repl->IP[current_slave]);
    Test->repl->CloseConn();
    return(current_slave);
}

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

    char sys1[100];
    printf("Killing VM\n");
    sprintf(&sys1[0], "%s %s", Test->KillVMCommand, Test->repl->IP[old_slave]);
    system(sys1);
    printf("Sleeping 60 seconds to let MaxScale to find new slave\n");
    sleep(60);

    current_slave = FindConnectedSlave(Test, &global_result);
    if (current_slave == old_slave) {printf("FAILED: No failover happened\n"); global_result=1;}

    Test->CloseRWSplit();

    exit(global_result);
}
