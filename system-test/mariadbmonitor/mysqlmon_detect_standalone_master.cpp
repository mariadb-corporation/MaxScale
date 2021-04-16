/**
 * @file mysqlmon_detect_standalone_master.cpp MySQL Monitor Standalone Master Test
 * - block all nodes, but one
 * - wait for monitor (monitor_interval), monitor should select remaining node as master
 * - check that queries work
 * - unblock backend nodes
 * - wait for monitor
 * - check that monitor is still using the same node and that the old nodes are in maintenance mode
 */

#include <iostream>
#include <maxtest/testconnections.hh>
#include <maxtest/mariadb_connector.hh>

using std::stringstream;
using std::cout;
using std::endl;

void replicate_from(TestConnections& test, int server_ind, int target_ind)
{
    test.try_query(test.repl->nodes[server_ind], "STOP SLAVE;");

    stringstream change_master;
    change_master << "CHANGE MASTER TO MASTER_HOST = '" << test.repl->ip_private(target_ind)
                  << "', MASTER_PORT = " << test.repl->port[target_ind]
                  << ", MASTER_USE_GTID = current_pos, MASTER_USER='repl', MASTER_PASSWORD='repl';";
    test.try_query(test.repl->nodes[server_ind], "%s", change_master.str().c_str());
    test.try_query(test.repl->nodes[server_ind], "START SLAVE;");
    cout << "Server " << server_ind + 1 << " starting to replicate from server " << target_ind + 1 << endl;
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto& mxs = test.maxscale();
    mxs.wait_monitor_ticks();
    mxs.get_servers().print();
    mxs.check_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        auto conn = mxs.open_rwsplit_connection();
        test.tprintf(" Create the test table and insert some data ");
        conn->cmd("CREATE OR REPLACE TABLE test.t1 (id int);");
        conn->cmd("INSERT INTO test.t1 VALUES (1);");
        test.repl->sync_slaves();

        mxs.wait_monitor_ticks();
        mxs.get_servers().print();
    }

    if (test.ok())
    {
        test.tprintf("Block all but one node, stop slave on server 4.");
        test.repl->block_node(0);
        test.repl->block_node(1);
        test.repl->block_node(2);
        auto srv4_conn = test.repl->backend(3)->try_open_admin_connection();
        srv4_conn->cmd("STOP SLAVE;");
        srv4_conn->cmd("RESET SLAVE ALL;");

        test.tprintf("Wait for the monitor to detect it ");
        mxs.wait_monitor_ticks(2);
        auto down = mxt::ServerInfo::DOWN;
        auto master = mxt::ServerInfo::master_st;
        mxs.check_servers_status({down, down, down, master});

        if (test.ok())
        {
            test.tprintf("Connect and insert should work.");
            auto conn = mxs.open_rwsplit_connection();
            conn->cmd("INSERT INTO test.t1 VALUES (1)");
            mxs.wait_monitor_ticks();
            mxs.get_servers().print();
        }

        test.tprintf("Unblock nodes");
        test.repl->unblock_node(0);
        test.repl->unblock_node(1);
        test.repl->unblock_node(2);
        mxs.wait_monitor_ticks();

        if (test.ok())
        {
            test.tprintf("Check that we are still using the last node to which we failed over to.");
            auto running = mxt::ServerInfo::RUNNING;
            mxs.check_servers_status({running, running, running, master});
        }

        // Try to reset situation.
        test.repl->connect();
        replicate_from(test, 0, 3);
        replicate_from(test, 1, 3);
        replicate_from(test, 2, 3);
        test.maxscales->wait_for_monitor();
        mxs.maxctrl("call command mariadbmon switchover MySQL-Monitor server1");
        test.maxscales->wait_for_monitor();
        auto status = mxs.get_servers();
        status.print();
        status.check_servers_status(mxt::ServersInfo::default_repl_states());
    }
    return test.global_result;
}
