/**
 * @file bug526.cpp regression case for bug 526 ( " Wrong module name crashes maxscale on connect" )
 *
 * - Maxscale.cnf with "filters=QLA|testfilter" for RWSplit router service, 'testfilter' is not defined.
 * - check error log for proper error messages and checks if ReadConn services are alive
 */

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    int global_result = 0;

    TestConnections * Test = new TestConnections(argc, argv);
    Test->read_env();
    Test->print_env();

    if (Test->connect_rwsplit() == 0) {
        printf("FAILED: filter config is broken, but service is started\n");
        global_result++;
    }

    sleep(5);
    global_result += check_log_err((char *) "Error : Unable to find library for module: foobar", TRUE);
    global_result += check_log_err((char *) "Error : Failed to create filter 'testfilter' for service 'RW Split Router", TRUE);
    //global_result += check_log_err((char *) "Error : Setting up filters failed. Terminating session RW Split Router", TRUE);

    //global_result += check_maxscale_alive();

    printf("Trying ReaConn master\n");
    if (Test->connect_readconn_master() != 0) {
        global_result++;
        printf("Error connection to ReadConn master\n");
    }
    printf("Trying ReaConn slave\n");
    if (Test->connect_readconn_slave() != 0) {
        global_result++;
        printf("Error connection to ReadConn slave\n");
    }
    Test->close_readconn_master();
    Test->close_readconn_slave();

    //Test->close_maxscale_connections();
    Test->copy_all_logs(); return(global_result);
}
