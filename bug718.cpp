/**
 * @file bug718.cpp bug718 regression case
 *
 */


#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "sql_t1.h"

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

    MYSQL * galera_rwsplit = open_conn(4016, Test->Maxscale_IP, Test->Maxscale_User, Test->Maxscale_Password);

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
    for (i = 0; i < 1000; i++)  {
        global_result += execute_query(galera_rwsplit, (char *) "show status");
    }

    create_t1(Test->conn_rwsplit);
    insert_into_t1(Test->conn_rwsplit, 4);

    create_t1(galera_rwsplit);
    insert_into_t1(galera_rwsplit, 4);

    Test->CloseMaxscaleConn();

    CheckMaxscaleAlive();

    Test->Copy_all_logs(); return(global_result);
}

