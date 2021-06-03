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
    test->stop_timeout();

    test->tprintf("Creating rules\n");
    test->maxscales->stop();

    sprintf(rules_dir, "%s/fw/", test_dir);
    test->maxscales->copy_fw_rules("rules_actions", rules_dir);

    test->set_timeout(60);
    test->maxscales->start_maxscale();

    test->set_timeout(30);
    test->maxscales->connect_maxscale();

    /** Test blacklisting functionality */
    test->tprintf("Trying matching query to blacklisted RWSplit, expecting failure\n");
    test->set_timeout(30);
    test->add_result(!execute_query_silent(test->maxscales->conn_rwsplit[0], "select 1"),
                     "Matching query to blacklist service should fail.\n");
    test->tprintf("Trying non-matching query to blacklisted RWSplit, expecting success\n");
    test->set_timeout(30);
    test->add_result(execute_query_silent(test->maxscales->conn_rwsplit[0], "show status"),
                     "Non-matching query to blacklist service should succeed.\n");

    /** Test whitelisting functionality */
    test->tprintf("Trying matching query to whitelisted Conn slave, expecting success\n");
    test->set_timeout(30);
    test->add_result(execute_query_silent(test->maxscales->conn_slave, "select 1"),
                     "Query to whitelist service should succeed.\n");
    test->tprintf("Trying non-matching query to whitelisted Conn slave, expecting failure\n");
    test->set_timeout(30);
    test->add_result(!execute_query_silent(test->maxscales->conn_slave, "show status"),
                     "Non-matching query to blacklist service should fail.\n");

    /** Testing NO OP mode */
    test->tprintf("Trying matching query to ignoring Conn master, expecting success\n");
    test->set_timeout(30);
    test->add_result(execute_query_silent(test->maxscales->conn_master, "select 1"),
                     "Query to ignoring service should succeed.\n");
    test->tprintf("Trying non-matching query to ignoring Conn master, expecting success\n");
    test->set_timeout(30);
    test->add_result(execute_query_silent(test->maxscales->conn_master, "show status"),
                     "Non-matching query to ignoring service should succeed.\n");

    test->stop_timeout();
    test->maxscales->expect_running_status(true);
    test->maxscales->stop();
    test->maxscales->expect_running_status(false);
    int rval = test->global_result;
    delete test;
    return rval;
}
