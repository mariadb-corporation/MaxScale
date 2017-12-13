#include "fail_switch_rejoin_common.cpp"

int prepare_test_1(TestConnections& test)
{
    delete_slave_binlogs(test);
    test.tprintf("Test 1: Stopping master and waiting for failover. Check that another server is promoted.\n"
                 "%s", LINE);
    get_input();
    int node0_id = test.repl->get_server_id(0); // Read master id now before shutdown.
    test.repl->stop_node(0);
    return node0_id;
}

void check_test_1(TestConnections& test, int node0_id)
{
    check(test);
    get_output(test);
    int master_id = get_master_server_id(test);
    test.tprintf(PRINT_ID, master_id);
    test.add_result(master_id < 1 && master_id == node0_id, "Master did not change or no master detected.");
    fix_replication_create_table(test);
    test.repl->connect();
}

void prepare_test_2(TestConnections& test)
{
    delete_slave_binlogs(test);
    test.tprintf("Test 2: Disable replication on server 2 and kill master, check that server 3 or 4 is "
                 "promoted.\n%s", LINE);
    get_input();
    execute_query(test.repl->nodes[1], "STOP SLAVE; RESET SLAVE ALL;");
    sleep(2);
    test.repl->stop_node(0);
}

void check_test_2(TestConnections& test)
{
    check(test);
    get_output(test);

    int master_id = get_master_server_id(test);
    test.tprintf(PRINT_ID, master_id);
    test.add_result(master_id < 1 ||
                    (master_id != test.repl->get_server_id(2) && master_id != test.repl->get_server_id(3)),
                    WRONG_SLAVE);
    fix_replication_create_table(test);
    test.repl->connect();
}
void prepare_test_3(TestConnections& test)
{
    delete_slave_binlogs(test);
    test.tprintf("Test3: Shutdown two slaves (servers 2 and 4). Disable log_bin on server 2, making it "
                 "invalid for promotion. Enable log-slave-updates on servers 2 and 4. Check that server 4 is "
                 "promoted on master failure.\n%s", LINE);
    get_input();

    test.repl->stop_node(1);
    test.repl->stop_node(3);
    test.repl->stash_server_settings(1);
    test.repl->stash_server_settings(3);
    test.repl->disable_server_setting(1, "log-bin");
    const char* log_slave = "log_slave_updates=1";
    test.repl->add_server_setting(1, log_slave);
    test.repl->add_server_setting(3, log_slave);
    test.repl->start_node(1, (char *) "");
    test.repl->start_node(3, (char *) "");
    sleep(4);
    test.tprintf("Settings changed.");
    get_output(test);
    test.tprintf("Stopping master.");
    test.repl->stop_node(0);
}

void check_test_3(TestConnections& test)
{
    check(test);
    get_output(test);

    int master_id = get_master_server_id(test);
    // Because servers have been restarted, redo connections.
    test.repl->connect();
    sleep(2);
    test.tprintf(PRINT_ID, master_id);
    test.add_result(master_id < 1 || master_id != test.repl->get_server_id(3), WRONG_SLAVE);
    // Restore server 2 and 4 settings. Because server 4 is now the master, shutting it down causes
    // another failover. Prevent this by stopping maxscale.
    test.tprintf("Restoring server settings.");
    test.maxscales->stop_maxscale(0);
    test.repl->stop_node(1);
    test.repl->stop_node(3);
    sleep(4);
    test.repl->restore_server_settings(1);
    test.repl->restore_server_settings(3);
    test.repl->start_node(0, (char *) "");
    test.repl->start_node(1, (char *) "");
    test.repl->start_node(3, (char *) "");
    sleep(4);
    test.maxscales->start_maxscale(0);
    sleep(2);
    get_output(test);
}
