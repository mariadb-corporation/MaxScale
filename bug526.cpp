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
        Test->tprintf("FAILED: filter config is broken, but service is started\n");
        global_result++;
    }

    sleep(5);
    global_result += Test->check_log_err((char *) "Error : Unable to find library for module: foobar", TRUE);
    if ((Test->check_log_err((char *) "Error : Failed to create filter 'testfilter' for service 'RW Split Router", TRUE) != 0) && (Test->check_log_err((char *) "Error : Failed to load filter 'testfilter' for service 'RW Split Router", TRUE))) {
        global_result++;
    }

    //global_result +=Test->check_log_err((char *) "Error : Setting up filters failed. Terminating session RW Split Router", TRUE);

    //global_result +=Test->check_maxscale_alive();

    Test->tprintf("Trying ReaConn master\n");
    if (Test->connect_readconn_master() != 0) {
        global_result++;
        Test->tprintf("Error connection to ReadConn master\n");
    }
    Test->tprintf("Trying ReaConn slave\n");
    if (Test->connect_readconn_slave() != 0) {
        global_result++;
        Test->tprintf("Error connection to ReadConn slave\n");
    }
    Test->close_readconn_master();
    Test->close_readconn_slave();

    //Test->close_maxscale_connections();
    Test->copy_all_logs(); return(global_result);
}
