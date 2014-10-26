#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();
    Test->ConnectMaxscale();

    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server =(");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server =)");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server =:");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server =a");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server = a");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server = кириллица åäö");


    Test->CloseMaxscaleConn();

    global_result += CheckMaxscaleAlive();

    return(global_result);
}

