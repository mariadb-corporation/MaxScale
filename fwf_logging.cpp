/**
 * Firewall filter logging test
 *
 * Check if the log_match and log_no_match parameters work
 */

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include "testconnections.h"

int main(int argc, char** argv)
{
    int rval = 0;
    TestConnections *test = new TestConnections(argc, argv);
    const int sleep_time = 15;

    test->tprintf("Creating rules\n");
    test->stop_maxscale();
    test->ssh_maxscale(false, "echo \"rule r1 deny regex 'select.*1'\" > %s/rules/rules.txt", test->maxscale_access_homedir);
    test->ssh_maxscale(false, "echo \"users %%@%% match any rules r1\" >> %s/rules/rules.txt", test->maxscale_access_homedir);
    test->start_maxscale();
    test->tprintf("Waiting for %d seconds", sleep_time);
    sleep(sleep_time);
    
    test->connect_maxscale();
    test->add_result(test->try_query(test->conn_slave, "select 1"), "First query should succeed");
    test->add_result(test->try_query(test->conn_slave, "select 2"), "Second query should succeed");
    test->check_log_err("matched by", true);
    test->check_log_err("was not matched", true);
    test->check_maxscale_alive();
    test->copy_all_logs();
    return test->global_result;
}
