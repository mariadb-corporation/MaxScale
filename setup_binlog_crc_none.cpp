/**
 * @file setup_binlog test of simple binlog router setup
 * setup one master, one slave directly connected to real master and two slaves connected to binlog router
 * create table and put data into it using connection to master
 * check data using direct commection to all backend
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->read_env();
    Test->print_env();


    Test->binlog_cmd_option = 2;
    Test->start_binlog();

    Test->repl->connect();

    create_t1(Test->repl->nodes[0]);
    global_result += insert_into_t1(Test->repl->nodes[0], 4);
    printf("Sleeping to let replication happen\n"); fflush(stdout);
    sleep(30);

    for (int i = 0; i < Test->repl->N; i++) {
        printf("Checking data from node %d (%s)\n", i, Test->repl->IP[i]); fflush(stdout);
        global_result += select_from_t1(Test->repl->nodes[i], 4);
    }

    Test->repl->close_connections();


    Test->copy_all_logs(); return(global_result);
}
