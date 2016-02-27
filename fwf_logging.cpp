/**
 * Firewall filter logging test
 *
 * Check if the log_match and log_no_match parameters work
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

    sprintf(rules_dir, "%s/fw/", test->test_dir);


    test->tprintf("Creating rules\n");
    test->stop_maxscale();
    copy_rules(test, (char*) "rules_logging", rules_dir);

    test->start_maxscale();
    test->tprintf("Waiting for %d seconds", sleep_time);
    sleep(sleep_time);
    
    test->connect_maxscale();
    test->tprintf("trying first: 'select 1'\n");
    test->add_result(test->try_query(test->conn_slave, "select 1"), "First query should succeed");
    test->tprintf("trying second: 'select 2'\n");
    test->add_result(test->try_query(test->conn_slave, "select 2"), "Second query should succeed");
    sleep(10);
    test->check_log_err("matched by", true);
    test->check_log_err("was not matched", true);
    test->check_maxscale_alive();
    test->copy_all_logs();
    return test->global_result;
}
