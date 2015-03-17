/**
 * @file sharding.cpp
 *
 * @verbatim

 @endverbatim
 * - check if Maxscale alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->repl->stop_slaves();

    Test->restart_maxscale();

    Test->repl->connect();

    for (int i = 0; i < Test->repl->N; i++) {
        execute_query(Test->repl->nodes, "CREATE USER 'user%d'@'%%' IDENTIFIED BY 'pass%d';", i, i);
        execute_query(Test->repl->nodes, "CREATE DATABASE 'db%d;", i);
        execute_query(Test->repl->nodes, "GRANT SELECT,USAGE ON db%d.* TO 'user%d'@'%%'", i, i);

    }

    global_result += check_maxscale_alive();

    Test->copy_all_logs(); return(global_result);
}
