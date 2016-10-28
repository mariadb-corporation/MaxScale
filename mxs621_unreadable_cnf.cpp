/**
 * @file max621_unreadable_cnf.cpp mxs621 regression case ("MaxScale fails to start silently if config file is not readable")
 *
 * - make maxscale.cnf unreadable
 * - try to restart Maxscale
 * - check log for error
 * - retore access rights to maxscale.cnf
 */

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(30);
    Test->ssh_maxscale(TRUE, "chmod 400 /etc/maxscale.cnf");
    Test->set_timeout(30);
    Test->restart_maxscale();
    Test->set_timeout(30);
    Test->check_log_err((char *) "Opening file '/etc/maxscale.cnf' for reading failed", TRUE);
    Test->set_timeout(30);
    Test->ssh_maxscale(TRUE, "chmod 777 /etc/maxscale.cnf");

    Test->copy_all_logs(); return(Test->global_result);
}
