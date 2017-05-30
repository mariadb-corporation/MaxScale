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
    Test->set_timeout(10);

    Test->add_result(Test->connect_maxscale(), "Can not connect to Maxscale\n");


    Test->tprintf("Trying queries that caused crashes before fix: bug473\n");

    Test->try_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server =(");
    Test->try_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server =)");
    Test->try_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server =:");
    Test->try_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server =a");
    Test->try_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server = a");
    Test->try_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale route to server = кириллица åäö");

    // bug472
    Test->tprintf("Trying queries that caused crashes before fix: bug472\n");
    Test->try_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale s1 begin route to server server3");
    Test->try_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale end");
    Test->try_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale s1 begin");

    // bug470
    Test->tprintf("Trying queries that caused crashes before fix: bug470\n"); fflush(stdout);
    Test->try_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale named begin route to master");
    Test->try_query(Test->conn_rwsplit, (char *) "select @@server_id;");
    Test->try_query(Test->conn_rwsplit, (char *) "select @@server_id; -- maxscale named begin route to master; select @@server_id;");


    Test->close_maxscale_connections();

    Test->tprintf("Checking if Maxscale is alive\n"); fflush(stdout);
    Test->check_maxscale_alive();

    Test->copy_all_logs(); return(Test->global_result);
}

