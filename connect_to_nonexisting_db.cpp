// some relations to bug#425
// connect to no-existing DB

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main()
{

    TestConnections * Test = new TestConnections();
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    char sql[100];

    Test->ConnectRWSplit();
    execute_query(Test->conn_rwsplit, (char *) "DROP DATABASE IF EXISTS test;");
    Test->CloseRWSplit();
    sleep(5);

    global_result += Test->ConnectMaxscale();
    global_result += execute_query(Test->conn_rwsplit, (char *) "CREATE DATABASE test;");
    global_result += create_t1(Test->conn_rwsplit);
    Test->CloseMaxscaleConn();

    global_result += Test->ConnectMaxscale();
    global_result += execute_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 (x1, fl) VALUES(0, 1);");

    global_result += execute_select_query_and_check(Test->conn_rwsplit, (char *) "SELECT * FROM t1;", 1);

    Test->CloseMaxscaleConn();

    return(global_result);
}
