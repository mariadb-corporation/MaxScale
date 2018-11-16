/**
 * @file keepalived:masterdown.cpp
 */


#include <iostream>
#include "testconnections.h"
#include "keepalived_func.h"
#include "failover_common.cpp"

static bool check_maxscale_passive(TestConnections& test, int node);
static void expect_maxscale_active_passive(TestConnections& test, int active_node);
const char ms_is_passive[] = "Maxscale %i is passive when active was expected.";
const char ms_is_active[] = "Maxscale %i is active when passive was expected.";

int main(int argc, char* argv[])
{
    const int failover_mon_ticks = 2;
    // Keepalived takes more time to switch primary MaxScale than a normal failover.
    // For now, assume that the time scales with monitor ticks.
    const int keepalived_switch_mon_ticks = 6;

    TestConnections::multiple_maxscales(true);
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);

    if (test.maxscales->N < 2)
    {
        test.tprintf("At least 2 Maxscales are needed for this test. Exiting");
        exit(0);
    }

    test.on_destroy([&](){
                        test.maxscales->ssh_node_f(0, true, "service keepalived stop");
                        test.maxscales->ssh_node_f(1, true, "service keepalived stop");
                    });

    test.repl->connect();
    delete_slave_binlogs(test);
    basic_test(test);
    print_gtids(test);

    test.tprintf("Configuring 'keepalived'\n");
    // Get test client IP, replace last number in it with 253 and use it as Virtual IP
    configure_keepalived(&test, (char*) "masterdown");

    print_version_string(&test);
    test.maxscales->wait_for_monitor(1, 0);
    test.maxscales->wait_for_monitor(1, 1);

    // initial state: 000 expected to be active, 001 - passive
    int active_node = 0;
    expect_maxscale_active_passive(test, active_node);
    if (test.global_result != 0)
    {
        return test.global_result;
    }

    // Test a normal failover.
    int first_master = test.repl->find_master();
    test.tprintf("Stop Master - node %d\n", first_master);
    test.repl->stop_node(first_master);
    test.maxscales->wait_for_monitor(failover_mon_ticks, active_node);

    int second_master = test.repl->find_master();
    test.tprintf("new master is node %d\n", second_master);
    test.expect(first_master != second_master, "Master did not change, failover did not happen.");

    char str[1024];
    sprintf(str, "Performing automatic failover to replace failed master 'server%d'", first_master + 1);
    test.tprintf("Checking Maxscale log on 000 for the failover message %s\n", str);
    test.log_includes(0, str);
    sprintf(str, "Performing automatic failover to replace failed master");
    test.tprintf("Checking Maxscale log on 001 for the lack of failover message\n");
    test.log_excludes(1, str);
    if (test.global_result != 0)
    {
        return test.global_result;
    }

    // Stop MaxScale 0. Keepalived should promote MaxScale 1 to primary.
    test.tprintf("Stop Maxscale 000\n");
    test.maxscales->stop_maxscale(0);
    active_node = 1;
    test.maxscales->wait_for_monitor(keepalived_switch_mon_ticks, active_node);
    test.expect(!check_maxscale_passive(test, active_node), ms_is_passive, active_node);
    if (test.global_result != 0)
    {
        return test.global_result;
    }

    // Check that a failover still happens.
    test.tprintf("Stop new Master - node %d\n", second_master);
    test.repl->stop_node(second_master);
    test.maxscales->wait_for_monitor(failover_mon_ticks, active_node);

    int third_master = test.repl->find_master();
    test.tprintf("new master (third one) is node %d\n", third_master);
    test.expect(third_master != second_master, "Master did not change, failover did not happen.");

    sprintf(str, "Performing automatic failover to replace failed master 'server%d'", second_master + 1);
    test.tprintf("Checking Maxscale log on 001 for the failover message %s\n", str);
    test.log_includes(1, str);

    test.log_excludes(1, "Multiple failed master servers detected");
    test.log_excludes(1, "Failed to perform failover");
    test.log_excludes(1, "disabling automatic failover");
    if (test.global_result != 0)
    {
        return test.global_result;
    }

    // Bring MaxScale 0 back up, check that it regains primary status.
    test.tprintf("Start Maxscale 000\n");
    test.maxscales->start_maxscale(0);
    active_node = 0;
    test.maxscales->wait_for_monitor(keepalived_switch_mon_ticks, active_node);
    expect_maxscale_active_passive(test, active_node);
    if (test.global_result != 0)
    {
        return test.global_result;
    }

    sprintf(str, "Performing automatic failover to replace failed master 'server%d'", second_master + 1);
    test.tprintf("Checking Maxscale log on 001 for the failover message %s\n", str);
    test.log_includes(1, str);
    test.tprintf("Checking Maxscale log on 000 for the lack of failover message %s\n", str);
    test.log_excludes(0, str);

    test.log_excludes(1, "Multiple failed master servers detected");
    test.log_excludes(1, "Failed to perform failover");
    test.log_excludes(1, "disabling automatic failover");

    test.log_excludes(0, "Multiple failed master servers detected");
    test.log_excludes(0, "Failed to perform failover");
    test.log_excludes(0, "disabling automatic failover");

    stop_keepalived(&test);
    return test.global_result;
}

bool check_maxscale_passive(TestConnections& test, int node)
{
    bool passive = false;
    char* passive_str = NULL;
    int ec = -1;

    test.tprintf("Checking status of Maxscale %03d", node);
    passive_str = test.maxscales->ssh_node_output(node, "maxctrl show maxscale | grep passive", false, &ec);
    test.tprintf("maxctrl output string: %s\n", passive_str);
    if (strstr(passive_str, "false") != NULL)
    {
        passive = false;
    }
    else
    {
        if (strstr(passive_str, "true") == NULL)
        {
            test.tprintf("Can't find 'true' or 'false' in the 'maxctrl' output\n");
        }
        passive = true;
    }
    free(passive_str);

    test.tprintf("Content of 'state.txt' file: %s\n",
                 test.maxscales->ssh_node_output(0, "cat /tmp/state.txt", false, &ec));
    return passive;
}

void expect_maxscale_active_passive(TestConnections& test, int active_node)
{
    int passive_node = active_node == 0 ? 1 : 0;
    test.expect(!check_maxscale_passive(test, active_node), ms_is_passive, active_node);
    test.expect(check_maxscale_passive(test, passive_node), ms_is_active, passive_node);
}
