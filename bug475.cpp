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

    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->read_env();
    Test->print_env();
    Test->connect_maxscale();

    global_result += execute_query(Test->conn_rwsplit, (char *) "select /* maxscale hintname prepare route to master */ @@server_id;");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select /* maxscale hintname begin */ @@server_id;");
    global_result += execute_query(Test->conn_rwsplit, (char *) "select /* maxscale route to master*/ @@server_id;");

    global_result += check_log_err((char *) "Error : Syntax error in hint", FALSE);
    global_result += check_maxscale_alive();
    Test->copy_all_logs(); return(global_result);
}
