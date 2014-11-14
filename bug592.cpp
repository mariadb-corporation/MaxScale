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
    Test->repl->Connect();
    Test->ConnectMaxscale();


    for (i = 1; i < Test->repl->N; i++) {
        execute_query(Test->repl->nodes[i], (char *) "stop slave;");
    }

    execute_query(Test->conn_rwsplit, (char *) "CREATE USER 'test_user'@'%' IDENTIFIED BY 'pass'");

    MYSQL * conn = open_conn_no_db(Test->rwsplit_port, Test->Maxscale_IP, (char *) "test_user", (char *) "pass");

    if (conn == NULL) {
        printf("Connections error\n");
        global_result++;
    }

    for (i = 1; i < Test->repl->N; i++) {
        execute_query(Test->repl->nodes[i], (char *) "start slave;");
    }

    Test->repl->CloseConn();
    Test->CloseMaxscaleConn();

    return(global_result);

}

