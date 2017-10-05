/**
 * Firewall filter match action test
 *
 * Check if the blacklisting, whitelisting and ignoring  funcionality of
 * the dbfwfilter works. This test executes a matching and a non-matching query
 * to three services configured in block, allow and ignore modes.
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

    test->tprintf("Creating rules\n");
    test->stop_maxscale();

    sprintf(rules_dir, "%s/fw/", test_dir);
    copy_rules(test, (char*) "rules_actions", rules_dir);

    test->set_timeout(60);
    test->start_maxscale();

    test->set_timeout(30);
    test->connect_maxscale();

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
    test->add_result(execute_query_silent(test->maxscales->conn_slave[0], "select 1"),
                     "Query to whitelist service should succeed.\n");
    test->tprintf("Trying non-matching query to whitelisted Conn slave, expecting failure\n");
    test->set_timeout(30);
    test->add_result(!execute_query_silent(test->maxscales->conn_slave[0], "show status"),
                     "Non-matching query to blacklist service should fail.\n");

    /** Testing NO OP mode */
    test->tprintf("Trying matching query to ignoring Conn master, expecting success\n");
    test->set_timeout(30);
    test->add_result(execute_query_silent(test->maxscales->conn_master[0], "select 1"),
                     "Query to ignoring service should succeed.\n");
    test->tprintf("Trying non-matching query to ignoring Conn master, expecting success\n");
    test->set_timeout(30);
    test->add_result(execute_query_silent(test->maxscales->conn_master[0], "show status"),
                     "Non-matching query to ignoring service should succeed.\n");

    test->stop_timeout();
    test->tprintf("Checking if MaxScale is alive\n");
    test->check_maxscale_processes(1);
    test->stop_maxscale();
    sleep(10);
    test->tprintf("Checking if MaxScale was succesfully terminated\n");
    test->check_maxscale_processes(0);
    int rval = test->global_result;
    delete test;
    return rval;
}
