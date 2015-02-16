/**
 * @file bug422.cpp bug422 regression case ( Executing '\s' doesn't always produce complete result set)
 *
 * Test executes "show status" 1000 times against all Maxscale services and checks Maxscale is alive after it.
 */


#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
    int global_result = 0;
    int i;

    Test->ReadEnv();
    Test->PrintIP();

    printf("Connecting to all MaxScale services\n"); fflush(stdout);
    global_result += Test->ConnectMaxscale();

    printf("executing show status 1000 times\n"); fflush(stdout);


    for (i = 0; i < 1000; i++)  {
        global_result += execute_query(Test->conn_rwsplit, (char *) "show status");
    }
    for (i = 0; i < 1000; i++)  {
        global_result += execute_query(Test->conn_slave, (char *) "show status");
    }
    for (i = 0; i < 1000; i++)  {
        global_result += execute_query(Test->conn_master, (char *) "show status");
    }

    Test->CloseMaxscaleConn();

    CheckMaxscaleAlive();

    Test->Copy_all_logs(); return(global_result);
}
