/**
 * @file max720_wierd_line.cpp mxs720 regression case - second part: weird lines  ("MaxScale fails to start
 * and doesn't log any useful message when there are spurious characters in the config file")
 *
 * - use incorrect maxscale.cnf
 * - check log for error
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(30);
    Test->check_log_err(0, (char*) "Unexpected parameter 'укпоукц'", true);
    Test->check_log_err(0, (char*) "Unexpected parameter 'hren'", true);

    Test->check_maxscale_processes(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
