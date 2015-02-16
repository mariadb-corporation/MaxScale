/**
 * @file bug488.cpp regression case for bug 488 ( SHOW VARIABLES randomly failing with "Lost connection to MySQL server")
 *
 * - try "SHOW VARIABLES;" 100 times against all Maxscale services
 * First round: 100 iterations for RWSplit, then ReadConn Master, then ReadConn Slave
 * Second round: 100 iteration and in every iterations all three Maxscale services are in use.
 * - check if Maxscale is alive.
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
    int global_result = 0;
    int i;

    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();
    Test->ConnectMaxscale();

    printf("Trying SHOW VARIABLES to different Maxscale services\n");  fflush(stdout);
    printf("RWSplit\n");  fflush(stdout);
    for (i = 0; i < 100; i++) {global_result += execute_query(Test->conn_rwsplit, (char *) "SHOW VARIABLES;");}
    printf("ReadConn master\n");  fflush(stdout);
    for (i = 0; i < 100; i++) {global_result += execute_query(Test->conn_master, (char *) "SHOW VARIABLES;");}
    printf("ReadConn slave\n");  fflush(stdout);
    for (i = 0; i < 100; i++) {global_result += execute_query(Test->conn_slave, (char *) "SHOW VARIABLES;");}

    printf("All in one loop\n");  fflush(stdout);
    for (i = 0; i < 100; i++) {
        global_result += execute_query(Test->conn_rwsplit, (char *) "SHOW VARIABLES;");
        global_result += execute_query(Test->conn_master, (char *) "SHOW VARIABLES;");
        global_result += execute_query(Test->conn_slave, (char *) "SHOW VARIABLES;");
    }

    Test->CloseMaxscaleConn();
    Test->repl->CloseConn();

    global_result += CheckMaxscaleAlive();

    return(global_result);
}
