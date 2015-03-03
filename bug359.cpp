/**
 * @file bug359.cpp bug359 regression case (router_options in readwritesplit causes errors in error log)
 *
 * - Maxscale.cnf contains RWSplit router definition with raouter_option=slave.
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
    int global_result = check_log_err((char *) "Warning : Unsupported router option \"slave\"", TRUE);
    global_result    += check_log_err((char *) "Error : Couldn't find suitable Master", FALSE);
    global_result += check_maxscale_alive();
    Test->copy_all_logs(); return(global_result);
}
