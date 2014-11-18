/**
 * @file bug547.cpp regression case for bug 547 and bug 594 ( "get_dcb fails if slaves are not available" and "Maxscale fails to start without anything in the logs if there is no slave available" )
 *
 * - Maxscale.cnf contains wrong IP for all slave
 * - create table, do INSERT using RWSplit router
 * - do SELECT using all services
 */

// also relates to bug594
// all slaves in MaxScale config have wrong IP

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    printf("Connecting to all MaxScale services\n"); fflush(stdout);
    global_result += Test->ConnectMaxscale();

    printf("Creating table t1\n"); fflush(stdout);

    if (execute_query(Test->conn_rwsplit, (char *) "DROP IF EXIST TABLE t1; CREATE TABLE t1  (x INT); INSERT INTO t1 (x) VALUES (1)") != 0) {
        global_result++;
        printf("Query failed!\n");
    }

    printf("Select using RWSplit\n"); fflush(stdout);
    if (execute_query(Test->conn_rwsplit, (char *) "select * from t1") != 0) {
        global_result++;
        printf("Query failed!\n");
    }
    printf("Select using ReadConn master\n"); fflush(stdout);
    if (execute_query(Test->conn_master, (char *) "select * from t1") != 0) {
        global_result++;
        printf("Query failed!\n");
    }
    printf("Select using ReadConn slave\n"); fflush(stdout);
    if (execute_query(Test->conn_slave, (char *) "select * from t1") !=0) {
        global_result++;
        printf("Query failed!\n");
    }

    Test->CloseMaxscaleConn();

    return(global_result);
}
