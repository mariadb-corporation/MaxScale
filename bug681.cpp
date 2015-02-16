/**
 * @file bug681.cpp  - regression test for bug681 ("crash if max_slave_connections=10% and 4 or less backends are configured")
 *
 * - Configure RWSplit with max_slave_connections=10%
 * - check ReadConn master and ReadConn slave are alive and RWSplit is not started
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "mariadb_func.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    Test->ConnectMaxscale();

    if (Test->conn_rwsplit != NULL) {
        global_result++;
        printf("FAILED: RWSplit services should fail, but it is started\n"); fflush(stdout);
    }

    printf("Trying query to ReadConn master\n"); fflush(stdout);
    global_result += execute_query(Test->conn_master, "show processlist;");
    printf("Trying query to ReadConn slave\n"); fflush(stdout);
    global_result += execute_query(Test->conn_slave, "show processlist;");

    Test->CloseMaxscaleConn();

    global_result    += CheckLogErr((char *) "Error : Unable to start RW Split Router service. There are too few backend servers configured in MaxScale.cnf. Found 10% when at least 33% would be required", TRUE);

    return(global_result);
}
