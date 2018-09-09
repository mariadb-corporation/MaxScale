/**
 * @file mxs722.cpp MaxScale configuration check functionality test
 *
 * - Get baseline for test from a valid config
 * - Test wrong parameter name
 * - Test wrong router_options value
 * - Test wrong filter parameter
 * - Test missing config file
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections* test = new TestConnections(argc, argv);
    test->stop_timeout();
    test->maxscales->stop_maxscale(0);

    /** Copy original config so we can easily reset the testing environment */
    test->maxscales->ssh_node_f(0, true, "cp /etc/maxscale.cnf /etc/maxscale.cnf.backup");

    /** Get a baseline result with a good configuration */
    int baseline = test->maxscales->ssh_node_f(0, true, "maxscale -c --user=maxscale");

    /** Configure bad parameter for a listener */
    test->maxscales->ssh_node_f(0, true, "sed -i -e 's/service/ecivres/' /etc/maxscale.cnf");
    test->add_result(baseline == test->maxscales->ssh_node_f(0, true, "maxscale -c --user=maxscale"),
                     "Bad parameter name should be detected.\n");
    test->maxscales->ssh_node_f(0, true, "cp /etc/maxscale.cnf.backup /etc/maxscale.cnf");

    /** Set router_options to a bad value */
    test->maxscales->ssh_node_f(0,
                                true,
                                "sed -i -e 's/router_options.*/router_options=bad_option=true/' /etc/maxscale.cnf");
    test->add_result(baseline == test->maxscales->ssh_node_f(0, true, "maxscale -c --user=maxscale"),
                     "Bad router_options should be detected.\n");

    test->maxscales->ssh_node_f(0, true, "cp /etc/maxscale.cnf.backup /etc/maxscale.cnf");

    /** Configure bad filter parameter */
    test->maxscales->ssh_node_f(0, true, "sed -i -e 's/filebase/basefile/' /etc/maxscale.cnf");
    test->add_result(baseline == test->maxscales->ssh_node_f(0, true, "maxscale -c --user=maxscale"),
                     "Bad filter parameter should be detected.\n");

    /** Remove configuration file */
    test->maxscales->ssh_node_f(0, true, "rm -f /etc/maxscale.cnf");
    test->add_result(baseline == test->maxscales->ssh_node_f(0, true, "maxscale -c --user=maxscale"),
                     "Missing configuration file should be detected.\n");

    int rval = test->global_result;
    delete test;
    return rval;
}
