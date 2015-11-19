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
#include "maxadmin_operations.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    if (Test->connect_rwsplit() == 0) {
        Test->add_result(1, "Filter config is broken, but service is started\n");
    }
    if (Test->connect_readconn_master() == 0) {
        Test->add_result(1, "Filter config is broken, but Maxscale is started\n");
    }
    if (Test->connect_readconn_slave() == 0) {
        Test->add_result(1, "Filter config is broken, but Maxscale is started\n");
    }

    //sleep(5);
    execute_maxadmin_command(Test->maxscale_IP, (char*) "admin", Test->maxadmin_password, (char*) "sync logs");
    Test->check_log_err((char *) "Unable to find library for module: foobar", TRUE);
    Test->check_log_err((char *) "Failed to load filter module 'foobar'", TRUE);
    Test->check_log_err((char *) "Failed to load filter 'testfilter' for service 'RW Split Router'", TRUE);
    Test->check_log_err((char *) "Failed to open, read or process the MaxScale configuration file /etc/maxscale.cnf. Exiting", TRUE);

    Test->copy_all_logs(); return(Test->global_result);
}
