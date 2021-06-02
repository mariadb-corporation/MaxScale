/**
 * Firewall filter match action test
 *
 * Check if the blacklisting, whitelisting and ignoring  funcionality of
 * the dbfwfilter works. This test executes a matching and a non-matching query
 * to three services configured in block, allow and ignore modes.
 */


#include <iostream>
#include <unistd.h>
#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    char rules_dir[4096];

    TestConnections* test = new TestConnections(argc, argv);

    test->tprintf("Creating rules\n");
    test->maxscale->stop();

    sprintf(rules_dir, "%s/fw/", mxt::SOURCE_DIR);
    test->maxscale->copy_fw_rules("rules_actions", rules_dir);

    test->reset_timeout();
    test->maxscale->start_maxscale();

    test->reset_timeout();
    test->maxscale->connect_maxscale();

    /** Test blacklisting functionality */
    test->tprintf("Trying matching query to blacklisted RWSplit, expecting failure\n");
    test->reset_timeout();
    test->add_result(!execute_query_silent(test->maxscale->conn_rwsplit[0], "select 1"),
                     "Matching query to blacklist service should fail.\n");
    test->tprintf("Trying non-matching query to blacklisted RWSplit, expecting success\n");
    test->reset_timeout();
    test->add_result(execute_query_silent(test->maxscale->conn_rwsplit[0], "show status"),
                     "Non-matching query to blacklist service should succeed.\n");

    /** Test whitelisting functionality */
    test->tprintf("Trying matching query to whitelisted Conn slave, expecting success\n");
    test->reset_timeout();
    test->add_result(execute_query_silent(test->maxscale->conn_slave, "select 1"),
                     "Query to whitelist service should succeed.\n");
    test->tprintf("Trying non-matching query to whitelisted Conn slave, expecting failure\n");
    test->reset_timeout();
    test->add_result(!execute_query_silent(test->maxscale->conn_slave, "show status"),
                     "Non-matching query to blacklist service should fail.\n");

    /** Testing NO OP mode */
    test->tprintf("Trying matching query to ignoring Conn master, expecting success\n");
    test->reset_timeout();
    test->add_result(execute_query_silent(test->maxscale->conn_master, "select 1"),
                     "Query to ignoring service should succeed.\n");
    test->tprintf("Trying non-matching query to ignoring Conn master, expecting success\n");
    test->reset_timeout();
    test->add_result(execute_query_silent(test->maxscale->conn_master, "show status"),
                     "Non-matching query to ignoring service should succeed.\n");

    test->maxscale->expect_running_status(true);
    test->maxscale->stop();
    test->maxscale->expect_running_status(false);
    int rval = test->global_result;
    delete test;
    return rval;
}
