// some relations to bug#425
// connect to no-existing DB

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->read_env();
    Test->print_env();

    printf("Connecting to RWSplit\n");
    Test->conn_rwsplit = open_conn_no_db(Test->rwsplit_port, Test->maxscale_IP, Test->maxscale_user, Test->maxscale_password, Test->ssl);
    if (Test->conn_rwsplit == NULL) {
        printf("Error connecting to MaxScale\n"); return(1);
    }
    printf("Removing 'test' DB\n");
    execute_query(Test->conn_rwsplit, (char *) "DROP DATABASE IF EXISTS test;");
    printf("Closing connections and waiting 5 seconds\n");
    Test->close_rwsplit();
    sleep(5);

    printf("Connection to non-existing DB (all routers)\n");
    Test->connect_maxscale();
    Test->close_maxscale_connections();

    printf("Connecting to RWSplit again to recreate 'test' db\n");
    Test->conn_rwsplit = open_conn_no_db(Test->rwsplit_port, Test->maxscale_IP, Test->maxscale_user, Test->maxscale_password, Test->ssl);
    if (Test->conn_rwsplit == NULL) {
        printf("Error connecting to MaxScale\n"); return(1);
    }

    printf("Creating and selecting 'test' DB\n");
    global_result += execute_query(Test->conn_rwsplit, (char *) "CREATE DATABASE test; USE test");
    printf("Creating 't1' table\n");
    global_result += create_t1(Test->conn_rwsplit);
    Test->close_rwsplit();

    printf("Reconnectiong\n");
    global_result += Test->connect_maxscale();
    printf("Trying simple operations with t1 \n");
    global_result += execute_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 (x1, fl) VALUES(0, 1);");
    global_result += execute_select_query_and_check(Test->conn_rwsplit, (char *) "SELECT * FROM t1;", 1);

    Test->close_maxscale_connections();

    Test->copy_all_logs(); return(global_result);
}
