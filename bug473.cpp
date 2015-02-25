/**
 * @file bug473.cpp  bug470, 472, 473 regression cases ( malformed hints cause crash )
 *
 * Test tries different hints with syntax errors (see source for details)
 */

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();
    Test->ConnectMaxscale();


    printf("Trying queries that caused crashes before fix: bug473\n"); fflush(stdout);

    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server =(");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server =)");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server =:");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server =a");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server = a");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server = кириллица åäö");

    if (global_result == 0) {
        printf("bug473 ok\n");
    }

    // bug472
    printf("Trying queries that caused crashes before fix: bug472\n"); fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale s1 begin route to server server3");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale end");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale s1 begin");

    if (global_result == 0) {
        printf("bug472 ok\n");
    }

    // bug470
    printf("Trying queries that caused crashes before fix: bug470\n"); fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale named begin route to master");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id;");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale named begin route to master; select @@server_id;");

    if (global_result == 0) {
        printf("bug470 ok\n");
    }

    Test->CloseMaxscaleConn();

    printf("Checking if Maxscale is alive\n"); fflush(stdout);
    global_result += CheckMaxscaleAlive();

    Test->Copy_all_logs(); return(global_result);
}

