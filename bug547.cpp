// also relates to bug594

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

    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP IF EXIST TABLE t1; CREATE TABLE t1  (x INT); INSERT INTO t1 (x) VALUES (1)");

    printf("Select using RWSplit\n"); fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, (char *) "select * from t1");
    printf("Select using ReadConn master\n"); fflush(stdout);
    global_result += execute_query(Test->conn_master, (char *) "select * from t1");
    printf("Select using ReadConn slave\n"); fflush(stdout);
    global_result += execute_query(Test->conn_slave, (char *) "select * from t1");

    Test->CloseMaxscaleConn();

    return(global_result);
}
