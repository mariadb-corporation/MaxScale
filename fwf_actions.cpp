/**
 * Firewall filter whitelist test
 *
 * Check if the whitelisting and ignoring of queries works
 */

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "fw_copy_rules.h"

int main(int argc, char** argv)
{
    char rules_dir[4096];
    TestConnections *test = new TestConnections(argc, argv);
    const int sleep_time = 15;

    test->tprintf("Creating rules\n");
    test->stop_maxscale();

    sprintf(rules_dir, "%s/fw/", test->test_dir);
    copy_rules(test, (char*) "rules_actions", rules_dir);

    test->start_maxscale();
    test->tprintf("Waiting for %d seconds\n", sleep_time);
    sleep(sleep_time);
    
    test->connect_maxscale();
    test->add_result(!test->try_query(test->conn_rwsplit, "select 1"), "Query to blacklist service should fail.\n");
    test->add_result(test->try_query(test->conn_slave, "select 1"), "Query to whitelist service should work.\n");
    test->add_result(test->try_query(test->conn_master, "select 1"), "Query ingore service should work.\n");
    test->check_maxscale_alive();
    test->copy_all_logs();
    return test->global_result;
}
