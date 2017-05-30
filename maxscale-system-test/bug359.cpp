/**
 * @file bug359.cpp bug359 regression case (router_options in readwritesplit causes errors in error log)
 *
 * - Maxscale.cnf contains RWSplit router definition with router_option=slave.
 * - warning is expected in the log, but not an error. All Maxscale services should be alive.
 */


#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    Test->check_log_err((char *) "Unsupported router option \"slave\"", TRUE);
    Test->check_log_err((char *) "Couldn't find suitable Master", FALSE);
    Test->check_maxscale_alive();
    Test->copy_all_logs(); return(Test->global_result);
}
