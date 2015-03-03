/**
 * @file bug592.cpp  regression case for bug 592 ( "slave in "Running" state breaks authorization" )
 *
 * - stop all slaves: "stop slave;" directly to every node (now they are in "Running" state, not in "Russning, Slave")
 * - via RWSplit "CREATE USER 'test_user'@'%' IDENTIFIED BY 'pass'"
 * - try to connect using 'test_user' (expecting success)
 * - start all slaves: "start slave;" directly to every node
 * - via RWSplit: "DROP USER 'test_user'@'%'"
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    int i;

    Test->read_env();
    Test->print_env();
    Test->repl->connect();
    Test->connect_maxscale();


    for (i = 1; i < Test->repl->N; i++) {
        execute_query(Test->repl->nodes[i], (char *) "stop slave;");
    }

    execute_query(Test->conn_rwsplit, (char *) "CREATE USER 'test_user'@'%' IDENTIFIED BY 'pass'");

    MYSQL * conn = open_conn_no_db(Test->rwsplit_port, Test->maxscale_IP, (char *) "test_user", (char *) "pass");

    if (conn == NULL) {
        printf("Connections error\n");
        global_result++;
    }

    for (i = 1; i < Test->repl->N; i++) {
        execute_query(Test->repl->nodes[i], (char *) "start slave;");
    }

    execute_query(Test->conn_rwsplit, (char *) "DROP USER 'test_user'@'%'");

    Test->repl->close_connections();
    Test->close_maxscale_connections();

    Test->copy_all_logs(); return(global_result);

}
