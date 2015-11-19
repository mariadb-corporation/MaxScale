/**
 * @file bug493.cpp regression case for bug 493 ( Can have same section name multiple times without warning)
 *
 * - Maxscale.cnf in which 'server2' is defined twice and tests checks error log for proper error message.
 * - check if Maxscale is alive
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
    Test->check_log_err((char *) "Duplicate section found: server2", TRUE);
    Test->check_log_err((char *) "Failed to open, read or process the MaxScale configuration file /etc/maxscale.cnf. Exiting", TRUE);
    //Test->check_maxscale_alive();
    Test->copy_all_logs(); return(Test->global_result);
}
