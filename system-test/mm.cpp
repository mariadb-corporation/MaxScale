/**
 * Test a simple two-server multimaster topology with MariaDB-Monitor.
 */

#include <maxtest/testconnections.hh>

using mxt::ServerInfo;

void test_main(TestConnections& test);

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    auto master = ServerInfo::master_st;
    auto slave = ServerInfo::slave_st;
    auto down = ServerInfo::DOWN;
    auto relay = ServerInfo::RELAY;

    mxs.stop();

    const int n = 2;    // Use only two backends for this test.
    const int extras_begin = repl.N - n;
    for (int i = extras_begin; i < repl.N; i++)
    {
        test.tprintf("Stopping %s.", repl.backend(i)->cnf_name().c_str());
        repl.stop_node(i);
    }

    repl.connect();
    auto& conns = repl.nodes;
    for (int i = 0; i < n; i++)
    {
        execute_query(conns[i], "stop slave; reset slave all;");
    }

    execute_query(conns[0], "SET GLOBAL READ_ONLY=ON");

    repl.replicate_from(0, 1);
    repl.replicate_from(1, 0);
    repl.close_connections();

    mxs.start();
    mxs.check_print_servers_status({slave | relay, master});

    if (test.ok())
    {
        test.tprintf("Block slave");
        repl.block_node(0);
        mxs.wait_for_monitor();

        mxs.check_print_servers_status({down, master});

        test.tprintf("Unblock slave");
        repl.unblock_node(0);
        mxs.wait_for_monitor();

        test.tprintf("Block master");
        repl.block_node(1);
        mxs.wait_for_monitor();

        mxs.check_print_servers_status({slave, down});

        test.tprintf("Make node 1 master");
        repl.connect();
        execute_query(conns[0], "SET GLOBAL READ_ONLY=OFF");
        repl.close_connections();
        mxs.wait_for_monitor();

        test.tprintf("Unblock slave");
        repl.unblock_node(1);
        mxs.wait_for_monitor();

        test.tprintf("Make node 2 slave");
        repl.connect();
        execute_query(conns[1], "SET GLOBAL READ_ONLY=ON");
        repl.close_connections();
        mxs.wait_for_monitor();

        mxs.check_print_servers_status({master, slave | relay});
    }

    // Since no data was written to backends, it should be possible to reset the situation.
    for (int i = extras_begin; i < repl.N; i++)
    {
        test.tprintf("Starting %s.", repl.backend(i)->cnf_name().c_str());
        repl.start_node(i);
    }
    repl.connect();
    for (int i = extras_begin; i < repl.N; i++)
    {
        repl.replicate_from(i, 0);
    }
}
