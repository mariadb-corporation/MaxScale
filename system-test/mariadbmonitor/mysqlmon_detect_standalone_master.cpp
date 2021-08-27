/**
 * @file mysqlmon_detect_standalone_master.cpp MySQL Monitor Standalone Master Test
 * - block all nodes, but one
 * - wait for monitor (monitor_interval), monitor should select remaining node as master
 * - check that queries work
 * - unblock backend nodes
 * - wait for monitor
 * - check that monitor is still using the same node and that the old nodes are in maintenance mode
 */

#include <maxtest/testconnections.hh>
#include <maxtest/mariadb_connector.hh>

void test_main(TestConnections& test);

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto repl = test.repl;
    mxs.wait_for_monitor();
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        auto conn = mxs.open_rwsplit_connection2();
        test.tprintf(" Create the test table and insert some data ");
        conn->cmd("CREATE OR REPLACE TABLE test.t1 (id int);");
        conn->cmd("INSERT INTO test.t1 VALUES (1);");
        repl->sync_slaves();

        mxs.wait_for_monitor();
        mxs.get_servers().print();
    }

    if (test.ok())
    {
        test.tprintf("Block all but one node, stop slave on server 4.");
        repl->block_node(0);
        repl->block_node(1);
        repl->block_node(2);
        auto srv4_conn = repl->backend(3)->try_open_connection();
        srv4_conn->cmd("STOP SLAVE;");
        srv4_conn->cmd("RESET SLAVE ALL;");

        test.tprintf("Wait for the monitor to detect it ");
        mxs.wait_for_monitor(2);
        auto down = mxt::ServerInfo::DOWN;
        auto master = mxt::ServerInfo::master_st;
        mxs.check_servers_status({down, down, down, master});

        if (test.ok())
        {
            test.tprintf("Connect and insert should work.");
            auto conn = mxs.open_rwsplit_connection2();
            conn->cmd("INSERT INTO test.t1 VALUES (1)");
            mxs.wait_for_monitor();
            mxs.get_servers().print();
        }

        test.tprintf("Unblock nodes");
        repl->unblock_node(0);
        repl->unblock_node(1);
        repl->unblock_node(2);
        mxs.wait_for_monitor();

        if (test.ok())
        {
            test.tprintf("Check that we are still using the last node to which we failed over to.");
            auto running = mxt::ServerInfo::RUNNING;
            mxs.check_print_servers_status({running, running, running, master});
        }

        // Try to reset situation.
        repl->connect();
        repl->replicate_from(0, 3);
        repl->replicate_from(1, 3);
        repl->replicate_from(2, 3);
        mxs.wait_for_monitor();
        mxs.maxctrl("call command mariadbmon switchover MySQL-Monitor server1");
        mxs.wait_for_monitor();
        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    }
}
