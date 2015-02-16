/**
 * @file bug475.cpp regression case for bug 475 (The end comment tag in hints isn't properly detected)
 *
 * Test tries different hints with comments syntax and then checks log and checks if MaxScale is alive
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

    Test->ReadEnv();
    Test->PrintIP();
    Test->ConnectMaxscale();

    global_result += execute_query(Test->conn_rwsplit, (char *) "select /* maxscale hintname prepare route to master */ @@server_id;");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select /* maxscale hintname begin */ @@server_id;");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select /* maxscale route to master*/ @@server_id;");

    global_result += CheckLogErr((char *) "Error : Syntax error in hint", FALSE);
    global_result += CheckMaxscaleAlive();
    Test->Copy_all_logs(); return(global_result);
}
