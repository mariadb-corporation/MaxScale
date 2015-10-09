/**
 * @file mxs47.cpp Regression test for bug MXS-47 ("Session freeze when small tail packet")
 * - execute SELECT REPEAT('a',i), where 'i' is changing from 1 to 50000 using all Maxscale services
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    char str[1024];

    //Test->repl->connect();
    Test->connect_maxscale();

    for (int i = 1; i < 50000; i++) {
        Test->set_timeout(5);
        sprintf(str, "SELECT REPEAT('a',%d)", i);
        Test->try_query(Test->conn_rwsplit, str);
        Test->try_query(Test->conn_master, str);
        Test->try_query(Test->conn_slave, str);
        if ((i/100)*100 == i) {
            Test->tprintf("%d iterations done\n", i);
        }
    }

    Test->close_maxscale_connections();

    Test->copy_all_logs(); return(Test->global_result);
}
