#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;
    int i;

    Test->ReadEnv();
    Test->PrintIP();

    printf("Connecting to all MaxScale services\n"); fflush(stdout);
    global_result += Test->ConnectMaxscale();

    printf("executing \\s 100 times\n"); fflush(stdout);


    for (i = 0; i < 100; i++)  {
        global_result += execute_query(Test->conn_rwsplit, (char *) "\\s");
    }
    for (i = 0; i < 100; i++)  {
        global_result += execute_query(Test->conn_slave, (char *) "\\s");
    }
    for (i = 0; i < 100; i++)  {
        global_result += execute_query(Test->conn_master, (char *) "\\s");
    }

    Test->CloseMaxscaleConn();

    CheckMaxscaleAlive();

    return(global_result);
}
