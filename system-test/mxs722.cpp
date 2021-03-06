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
#include <maxtest/testconnections.hh>

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections* test = new TestConnections(argc, argv);
    test->maxscale->stop();

    /** Copy original config so we can easily reset the testing environment */
    test->maxscale->ssh_node_f(true, "cp /etc/maxscale.cnf /tmp/maxscale.cnf");
    test->maxscale->ssh_node_f(true, "chmod a+rw /tmp/maxscale.cnf");

    /** Get a baseline result with a good configuration */
    int baseline = test->maxscale->ssh_node_f(true, "maxscale -c --user=maxscale -f /tmp/maxscale.cnf");

    /** Configure bad parameter for a listener */
    test->maxscale->ssh_node_f(true, "sed -i -e 's/service/ecivres/' /tmp/maxscale.cnf");
    test->add_result(
        baseline == test->maxscale->ssh_node_f(true, "maxscale -c --user=maxscale -f /tmp/maxscale.cnf"),
        "Bad parameter name should be detected.\n");
    test->maxscale->ssh_node_f(true, "cp /etc/maxscale.cnf /tmp/maxscale.cnf");

    /** Set router_options to a bad value */
    test->maxscale->ssh_node_f(true,
                               "sed -i -e 's/router_options.*/router_options=bad_option=true/' /tmp/maxscale.cnf");
    test->add_result(
        baseline == test->maxscale->ssh_node_f(true, "maxscale -c --user=maxscale -f /tmp/maxscale.cnf"),
        "Bad router_options should be detected.\n");

    test->maxscale->ssh_node_f(true, "cp /etc/maxscale.cnf /tmp/maxscale.cnf");

    /** Configure bad filter parameter */
    test->maxscale->ssh_node_f(true, "sed -i -e 's/filebase/basefile/' /tmp/maxscale.cnf");
    test->add_result(
        baseline == test->maxscale->ssh_node_f(true, "maxscale -c --user=maxscale -f /tmp/maxscale.cnf"),
        "Bad filter parameter should be detected.\n");

    /** Remove configuration file */
    test->maxscale->ssh_node_f(true, "rm -f /tmp/maxscale.cnf");
    test->add_result(
        baseline == test->maxscale->ssh_node_f(true, "maxscale -c --user=maxscale -f /tmp/maxscale.cnf"),
        "Missing configuration file should be detected.\n");

    int rval = test->global_result;
    delete test;
    return rval;
}
