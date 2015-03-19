/**
 * @file mxs47.cpp
 *
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    char str[1024];
    int global_result = 0;

    Test->read_env();
    Test->print_env();
    Test->repl->connect();
    Test->connect_maxscale();

    for (int i = 1; i < 50000; i++) {
        sprintf(str, "SELECT REPEAT('a',%d)", i);
        execute_query(Test->conn_rwsplit, str);
        execute_query(Test->conn_master, str);
        execute_query(Test->conn_slave, str);
        if ((i/100)*100 == i) {
            printf("%d iterations done\n", i); fflush(stdout);
        }
    }

    Test->close_maxscale_connections();

    Test->copy_all_logs(); return(global_result);

}
