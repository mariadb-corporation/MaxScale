/**
 * @file bug422.cpp bug422 regression case ( Executing '\s' doesn't always produce complete result set)
 *
 * Test executes "show status" 1000 times against all Maxscale services and checks Maxscale is alive after it.
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
    int i;

    Test->read_env();
    Test->print_env();

    printf("Connecting to all MaxScale services\n"); fflush(stdout);
    global_result += Test->connect_maxscale();

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

    Test->close_maxscale_connections();

    check_maxscale_alive();

    Test->copy_all_logs(); return(global_result);
}
