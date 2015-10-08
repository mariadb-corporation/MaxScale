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
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    if (Test->connect_rwsplit() == 0) {
        Test->add_result(1, "Filter config is broken, but service is started\n");
    }

    sleep(5);
    Test->check_log_err((char *) "Error : Unable to find library for module: foobar", TRUE);

    Test->tprintf("Trying ReaConn master\n");
    Test->add_result(Test->connect_readconn_master(), "Error connection to ReadConn master\n");

    Test->tprintf("Trying ReaConn slave\n");
    Test->add_result(Test->connect_readconn_slave(), "Error connection to ReadConn slave\n");

    Test->close_readconn_master();
    Test->close_readconn_slave();

    Test->copy_all_logs(); return(Test->global_result);
}
