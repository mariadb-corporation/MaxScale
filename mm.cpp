/**
 * @file mm test of multi master monitor
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

    Test->start_mm();

    Test->repl->connect();

    Test->connect_rwsplit();

    create_t1(Test->conn_rwsplit);
    global_result += insert_into_t1(Test->conn_rwsplit, 4);
    printf("Sleeping to let replication happen\n"); fflush(stdout);
    sleep(30);

    for (int i = 0; i < 2; i++) {
        printf("Checking data from node %d (%s)\n", i, Test->repl->IP[i]); fflush(stdout);
        global_result += select_from_t1(Test->repl->nodes[i], 4);
    }

    printf("Checking data from rwsplit\n"); fflush(stdout);
    global_result += select_from_t1(Test->conn_rwsplit, 4);

    Test->repl->close_connections();
    mysql_close(Test->conn_rwsplit);


    Test->copy_all_logs(); return(global_result);
}
