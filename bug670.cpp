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

    /*printf("executing sql 1000 times (ReadConn Slave)\n"); fflush(stdout);
    for (i = 0; i < 1000; i++)  {
        global_result += execute_query(Test->conn_slave, bug670_sql);
    }*/

    printf("executing sql 1000 times (ReadConn Master)\n"); fflush(stdout);
    for (i = 0; i < 1000; i++)  {
        global_result += execute_query(Test->conn_master, bug670_sql);
    }

    printf("executing sql 1000 times (RWSplit)\n"); fflush(stdout);
    for (i = 0; i < 1000; i++)  {
        global_result += execute_query(Test->conn_rwsplit, bug670_sql);
    }

    const char * x = strstr(bug670_sql, "\n");
    const char * y;
    char sql[1024];
    while (x != NULL) {
        y = strstr(x, "\n");
        strncpy(sql, x, y-x);
        sql[y-x] = '\0';
        printf("%s\n", sql);
    }


    Test->CloseMaxscaleConn();

    CheckMaxscaleAlive();

    return(global_result);
}
