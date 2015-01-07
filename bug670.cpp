/**
 * @file bug670.cpp bug670 regression case ( Executing '\s' doesn't always produce complete result set)
 *
 *
 */


#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "bug670_sql.h"

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

    printf("executing sql 1000 times\n"); fflush(stdout);


    for (i = 0; i < 1000; i++)  {
        execute_query(Test->conn_slave, bug670_sql);
    }

    Test->CloseMaxscaleConn();

    CheckMaxscaleAlive();

    return(global_result);
}
