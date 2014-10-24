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

    printf("Trying SHOW VARIABLES to different Maxscale services\n");  fflush(stdout);
    printf("RWSplit\n");  fflush(stdout);
    for (i = 0; i < 100; i++) {global_result += execute_query(Test->conn_rwsplit, (char *) "SHOW VARIABLES;");}
    printf("ReadConn master\n");  fflush(stdout);
    for (i = 0; i < 100; i++) {global_result += execute_query(Test->conn_master, (char *) "SHOW VARIABLES;");}
    printf("ReadConn slave\n");  fflush(stdout);
    for (i = 0; i < 100; i++) {global_result += execute_query(Test->conn_slave, (char *) "SHOW VARIABLES;");}

    printf("All in one loop\n");  fflush(stdout);
    for (i = 0; i < 100; i++) {
        global_result += execute_query(Test->conn_rwsplit, (char *) "SHOW VARIABLES;");
        global_result += execute_query(Test->conn_master, (char *) "SHOW VARIABLES;");
        global_result += execute_query(Test->conn_slave, (char *) "SHOW VARIABLES;");
    }

    Test->CloseMaxscaleConn();

    global_result += CheckMaxscaleAlive();

    return(global_result);
}
