/**
 * @file max621_unreadable_cnf.cpp mxs621 regression case ("MaxScale fails to start silently if config file is
 *not readable")
 *
 * - make maxscale.cnf unreadable
 * - try to restart Maxscale
 * - check log for error
 * - retore access rights to maxscale.cnf
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(30);
    Test->maxscales->ssh_node_f(0, true, "chmod 400 /etc/maxscale.cnf");
    Test->set_timeout(30);
    Test->maxscales->restart_maxscale(0);
    Test->set_timeout(30);
    Test->check_log_err(0, (char*) "Opening file '/etc/maxscale.cnf' for reading failed", true);
    Test->set_timeout(30);
    Test->maxscales->ssh_node_f(0, true, "chmod 777 /etc/maxscale.cnf");

    int rval = Test->global_result;
    delete Test;
    return rval;
}
