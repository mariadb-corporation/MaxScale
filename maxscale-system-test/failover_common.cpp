#include "fail_switch_rejoin_common.cpp"
#include <sstream>
#include <iostream>

using std::stringstream;
using std::cout;
using std::endl;

void replicate_from(TestConnections& test, int server_ind, int target_ind)
{
    stringstream change_master;
    change_master << "CHANGE MASTER TO MASTER_HOST = '" << test.repl->IP[target_ind]
        << "', MASTER_PORT = " << test.repl->port[target_ind] << ", MASTER_USE_GTID = current_pos, "
        "MASTER_USER='repl', MASTER_PASSWORD='repl';";
    cout << "Server " << server_ind + 1 << " starting to replicate from server " << target_ind + 1 << endl;
    if (test.verbose)
    {
       cout << "Query is '" << change_master.str() << "'" << endl;
    }
    execute_query(test.repl->nodes[server_ind], "STOP SLAVE;");
    execute_query(test.repl->nodes[server_ind], change_master.str().c_str());
    execute_query(test.repl->nodes[server_ind], "START SLAVE;");
}

void reset_replication(TestConnections& test)
{
    int master_id = get_master_server_id(test);
    cout << "Reseting..." << endl;
    test.repl->start_node(0, (char*)"");
    sleep(5);
    test.repl->connect();
    get_output(test);
    // First set the old master to replicate from current master.
    if (test.global_result == 0)
    {
        int ind = master_id - 1;
        replicate_from(test, 0, ind);
        sleep(3);
        get_output(test);
        int ec;
        stringstream switchover;
        switchover << "maxadmin call command mysqlmon switchover MySQL-Monitor server1 server" << master_id;
        test.maxscales->ssh_node_output(0, switchover.str().c_str() , true, &ec);
        sleep(3);
        master_id = get_master_server_id(test);
        cout << "Master server id is now back to " << master_id << endl;
        test.assert(master_id == 1, "Switchover back to server1 failed");
    }
    get_output(test);
    StringSet node_states;
    for (int i = 2; i < 4; i++)
    {
        stringstream servername;
        servername << "server" << i;
        node_states = test.get_server_status(servername.str().c_str());
        bool states_ok = (node_states.find("Slave") != node_states.end());
        test.assert(states_ok, "Server %d is not replicating.", i);
    }
}

int prepare_test_1(TestConnections& test)
{
    cout << LINE << endl;
    cout << "Part 1: Stopping master and waiting for failover. Check that another server is promoted." <<
        endl;
    cout << LINE << endl;
    int node0_id = test.repl->get_server_id(0); // Read master id now before shutdown.
    test.repl->stop_node(0);
    return node0_id;
}

void check_test_1(TestConnections& test, int node0_id)
{
    get_output(test);
    int master_id = get_master_server_id(test);
    cout << "Master server id is " << master_id << endl;
    test.assert(master_id > 0 && master_id != node0_id, "Master did not change or no master detected.");
    if (test.global_result == 0)
    {
        check(test);
    }
    // Reset state
    reset_replication(test);
}

void prepare_test_2(TestConnections& test)
{
    cout << LINE << endl;
    cout << "Part 2: Disable replication on server 2 and kill master, check that server 3 or 4 is promoted."
        << endl;
    cout << LINE << endl;
    test.repl->connect();
    check(test);
    sleep(1);
    print_gtids(test);
    test.try_query(test.repl->nodes[1], "STOP SLAVE;");
    test.try_query(test.repl->nodes[1], "RESET SLAVE ALL;");
    sleep(1);
    get_output(test);
    if (test.global_result == 0)
    {
        cout << "Stopping master." << endl;
        test.repl->stop_node(0);
    }
}

void check_test_2(TestConnections& test)
{
    get_output(test);
    int master_id = get_master_server_id(test);
    cout << "Master server id is " << master_id << endl;
    bool success = (master_id > 0 &&
                    (master_id == test.repl->get_server_id(2) || master_id == test.repl->get_server_id(3)));
    test.assert(success, WRONG_SLAVE);
    if (test.global_result == 0)
    {
        check(test);
    }

    // Reset state
    replicate_from(test, 1, master_id - 1);
    sleep(3);
    get_output(test);
    StringSet node_states = test.get_server_status("server2");
    test.assert(node_states.find("Slave") != node_states.end(), "Server 2 is not replicating.");
    if (test.global_result == 0)
    {
        reset_replication(test);
    }
}

void prepare_test_3(TestConnections& test)
{
    cout << LINE << endl;
    cout << "Part 3: Disable log_bin on server 2, making it invalid for promotion. Enable log-slave-updates "
        " on servers 2 and 4. Check that server 4 is promoted on master failure." << endl << LINE << endl;
    get_output(test);
    test.maxscales->stop_maxscale(0);
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
    test.maxscales->start_maxscale(0);
    sleep(2);

    test.repl->connect();
    test.tprintf("Settings changed.");
    get_output(test);
    print_gtids(test);
    check(test);

    if (test.global_result == 0)
    {
        cout << "Stopping master." << endl;
        test.repl->stop_node(0);
    }
}

void check_test_3(TestConnections& test)
{
    check(test);
    get_output(test);
    int master_id = get_master_server_id(test);

    // Because servers have been restarted, redo connections.
    test.repl->connect();
    cout << "Master server id is " << master_id << endl;
    test.assert(master_id > 0 && master_id == test.repl->get_server_id(3), WRONG_SLAVE);
    print_gtids(test);

    reset_replication(test);
    get_output(test);

    // Restore server 2 and 4 settings. Because server 1 is now the master, shutting it down causes
    // another failover. Prevent this by stopping maxscale.
    test.tprintf("Restoring server settings.");
    test.maxscales->stop_maxscale(0);
    test.repl->stop_node(1);
    test.repl->stop_node(3);
    sleep(4);

    test.repl->restore_server_settings(1);
    test.repl->restore_server_settings(3);

    test.repl->start_node(1, (char *) "");
    test.repl->start_node(3, (char *) "");
    sleep(2);
    test.maxscales->start_maxscale(0);
}
