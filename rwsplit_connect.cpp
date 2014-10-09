#include "testconnections.h"

int main()
{
    int global_result = 0;
    TestConnections * Test = new TestConnections();
    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();

    printf("Connecting to RWSplit %s\n", Test->Maxscale_IP);
    Test->ConnectRWSplit();

    unsigned int conn_num;
    unsigned int all_conn=0;
    for (int i = 0; i < Test->repl->N; i++) {
        conn_num = get_conn_num(Test->repl->nodes[i], Test->Maxscale_IP, (char *) "test");
        printf("connections: %u\n", conn_num);
        if ((i == 0) && (conn_num != 1)) {global_result=1;}
        all_conn += conn_num;
    }
    if (all_conn != 2) {global_result=1;}

    Test->CloseRWSplit();
    Test->repl->CloseConn();

    exit(global_result);
}
