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
    int i;
    int iterations = 1000;
    if (Test->smoke) {iterations = 100;}

    Test->set_timeout(10);

    Test->tprintf("Connecting to all MaxScale services\n");
    Test->add_result(Test->connect_maxscale(), "Can not connect to Maxscale\n");

    Test->tprintf("executing show status %d times\n", iterations);


    for (i = 0; i < iterations; i++)  {
        Test->set_timeout(5);
        Test->add_result(execute_query(Test->conn_rwsplit, (char *) "show status"), "Query %d agains RWSplit failed\n", i);
    }
    for (i = 0; i < iterations; i++)  {
        Test->set_timeout(5);
        Test->add_result(execute_query(Test->conn_slave, (char *) "show status"), "Query %d agains ReadConn Slave failed\n", i);
    }
    for (i = 0; i < iterations; i++)  {
        Test->set_timeout(5);
        Test->add_result(execute_query(Test->conn_master, (char *) "show status"), "Query %d agains ReadConn Master failed\n", i);
    }
    Test->set_timeout(10);

    Test->close_maxscale_connections();
    Test->check_maxscale_alive();
    Test->copy_all_logs(); return(Test->global_result);
}
