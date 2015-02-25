/**
 * @file bug600.cpp regression case for bug 649 ("")
 *

 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"


TestConnections * Test ;

char sql[100000];

int main(int argc, char *argv[])
{
    Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->PrintIP();

    printf("Connecting to RWSplit %s\n", Test->Maxscale_IP);
    Test->ConnectRWSplit();

    global_result += create_t1(Test->conn_rwsplit);
    create_insert_string(sql, 10, 1);
    execute_query(Test->conn_rwsplit, sql);

    execute_query(Test->conn_rwsplit, (char *) "DROP DATABASE IF EXIST test1; CREATE DATABESE test1;");
    execute_query(Test->conn_rwsplit, (char *) "USE test1");
    create_insert_string(sql, 10, 2);
    execute_query(Test->conn_rwsplit, sql);

    printf("Setup firewall to block first slave\n"); fflush(stdout);
    Test->repl->BlockNode(1); fflush(stdout);
    execute_query(Test->conn_rwsplit, (char *) "USE test");
    printf("Setup firewall back to allow mysql\n"); fflush(stdout);
    Test->repl->UnblockNode(1); fflush(stdout);

    char res[1024];
    for (int i = 0; i < 1000; i++) {
        find_status_field(Test->conn_rwsplit, "SELECT fl FROM t1 WHERE x1=1;", "fl", &res[0]);
        printf("%s\n", res);
    }

    printf("Checking Maxscale is alive\n"); fflush(stdout);
    global_result += CheckMaxscaleAlive(); fflush(stdout);


    sleep(10);

    Test->Copy_all_logs(); return(global_result);
}
