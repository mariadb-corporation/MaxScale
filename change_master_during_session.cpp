#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;


int main(int argc, char *argv[])
{
    int global_result = 0;

    TestConnections * Test = new TestConnections(argv[0]);
    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();

    printf("Connecting to RWsplit\n");
    Test->ConnectRWSplit();

    global_result += create_t1(Test->conn_rwsplit);

    global_result += execute_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 (x1, fl) VALUES(0, 1);");
    printf("Changing master to node 1\n");
    Test->repl->ChangeMaster(1, 0);
    printf("executing 3 INSERTs\n");
    execute_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 (x1, fl) VALUES(0, 2);");
    execute_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 (x1, fl) VALUES(1, 2);");
    execute_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 (x1, fl) VALUES(2, 2);");
    printf("executing SELECT\n");
    execute_query(Test->conn_rwsplit, (char *) "SELECT * FROM t1;");

    Test->CloseRWSplit();
    Test->ConnectRWSplit();
    printf("Reconnecting and executing SELECT again\n");
    global_result += execute_query(Test->conn_rwsplit, (char *) "SELECT * FROM t1;");


    printf("Changing master back to node 0\n");
    Test->repl->ChangeMaster(0, 1);

    Test->repl->CloseConn();

    return(global_result);
}
