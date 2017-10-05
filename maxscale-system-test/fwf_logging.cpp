/**
 * Firewall filter logging test
 *
 * Check if the log_match and log_no_match parameters work
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "fw_copy_rules.h"

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    char rules_dir[4096];

    TestConnections *test = new TestConnections(argc, argv);
    test->stop_timeout();

    sprintf(rules_dir, "%s/fw/", test_dir);

    test->tprintf("Creating rules\n");
    test->stop_maxscale();
    copy_rules(test, (char*) "rules_logging", rules_dir);

    test->start_maxscale();
    test->set_timeout(20);
    test->connect_maxscale();

    test->tprintf("trying first: 'select 1'\n");
    test->set_timeout(20);
    test->add_result(execute_query_silent(test->maxscales->conn_slave[0], "select 1"), "First query should succeed\n");

    test->tprintf("trying second: 'select 2'\n");
    test->set_timeout(20);
    test->add_result(execute_query_silent(test->maxscales->conn_slave[0], "select 2"), "Second query should succeed\n");

    /** Check that MaxScale is alive */
    test->stop_timeout();
    test->check_maxscale_processes(1);

    /** Check that MaxScale was terminated successfully */
    test->stop_maxscale();
    sleep(10);
    test->check_maxscale_processes(0);

    /** Check that the logs contains entries for both matching and
     * non-matching queries */
    test->check_log_err("matched by", true);
    test->check_log_err("was not matched", true);

    int rval = test->global_result;
    delete test;
    return rval;
}
