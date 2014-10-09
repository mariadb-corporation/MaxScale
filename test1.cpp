#include <iostream>
#include "testconnections.h"

using namespace std;

int main()
{
    TestConnections * Test = new TestConnections();
    Test->ReadEnv();
    Test->PrintIP();
    //Test->galera->Connect();
    Test->repl->Connect();
    //Test->ConnectMaxscale();


    MYSQL * conn_read = Test->ConnectReadMaster();
    int conn_num;
    for (int i = 0; i < Test->repl->N; i++) {
        conn_num = get_conn_num(Test->repl->nodes[0], Test->Maxscale_IP, (char *) "test");
        printf("Connections to node %d:\t%d\n", i, conn_num);
    }
    mysql_close(conn_read);

    Test->repl->ChangeMaster(1, 0);

    conn_read = Test->ConnectReadMaster();
    for (int i = 0; i < Test->repl->N; i++) {
        conn_num = get_conn_num(Test->repl->nodes[0], Test->Maxscale_IP, (char *) "test");
        printf("Connections to node %d:\t%d\n", i, conn_num);
    }
    mysql_close(conn_read);

    Test->repl->ChangeMaster(0, 1);
}

