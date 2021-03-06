/**
 * Firewall filter logging test
 *
 * Check if the log_match and log_no_match parameters work
 */


#include <iostream>
#include <unistd.h>
#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    char rules_dir[4096];

    TestConnections* test = new TestConnections(argc, argv);

    sprintf(rules_dir, "%s/fw/", mxt::SOURCE_DIR);

    test->tprintf("Creating rules\n");
    test->maxscale->stop();
    test->maxscale->copy_fw_rules("rules_logging", rules_dir);

    test->maxscale->start_maxscale();
    test->reset_timeout();
    test->maxscale->connect_maxscale();

    test->tprintf("trying first: 'select 1'\n");
    test->reset_timeout();
    test->add_result(execute_query_silent(test->maxscale->conn_slave, "select 1"),
                     "First query should succeed\n");

    test->tprintf("trying second: 'select 2'\n");
    test->reset_timeout();
    test->add_result(execute_query_silent(test->maxscale->conn_slave, "select 2"),
                     "Second query should succeed\n");

    /** Check that MaxScale is alive */
    test->maxscale->expect_running_status(true);

    /** Check that MaxScale was terminated successfully */
    test->maxscale->stop();
    test->maxscale->expect_running_status(false);

    /** Check that the logs contains entries for both matching and
     * non-matching queries */
    test->log_includes("matched by");
    test->log_includes("was not matched");

    int rval = test->global_result;
    delete test;
    return rval;
}
