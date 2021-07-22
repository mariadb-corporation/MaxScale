/**
 * Test monitoring and failover with ignore_external_masters=true
 */
#include <maxtest/testconnections.hh>
#include <atomic>
#include <thread>

static std::atomic<bool> is_running(true);

void writer_func(TestConnections* test)
{
    while (is_running)
    {
        MYSQL* conn = open_conn(test->maxscale->rwsplit_port, test->maxscale->ip4(),
                                "test", "test", false);

        for (int i = 0; i < 100; i++)
        {
            if (execute_query_silent(conn, "INSERT INTO test.t1 VALUES (SELECT SLEEP(0.5))"))
            {
                sleep(1);
                break;
            }
        }
        mysql_close(conn);
    }
}

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    test.repl->connect();
    auto& mxs = *test.maxscale;
    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    auto down = mxt::ServerInfo::DOWN;

    // Create a table and a user and start a thread that does writes
    MYSQL* node0 = test.repl->nodes[0];
    execute_query(node0, "CREATE OR REPLACE TABLE test.t1 (id INT)");
    execute_query(node0, "DROP USER IF EXISTS 'test'@'%%'");
    execute_query(node0, "CREATE USER 'test'@'%%' IDENTIFIED BY 'test'");
    execute_query(node0, "GRANT INSERT, SELECT, UPDATE, DELETE ON *.* TO 'test'@'%%'");
    test.repl->sync_slaves();
    std::thread thr(writer_func, &test);

    test.tprintf("Start by having the current master replicate from the external server.");
    test.repl->replicate_from(0, 3);
    test.maxscale->wait_for_monitor(1);
    mxs.check_print_servers_status({master, slave, slave});

    test.tprintf("Stop server1, expect server2 to be promoted as the master");
    test.repl->stop_node(0);
    test.maxscale->wait_for_monitor(2);

    mxs.check_print_servers_status({down, master, slave});

    test.tprintf("Configure master-master replication between server2 and the external server");
    // Comment away next line since failover already created the external connection. Failover/switchover
    // does not respect 'ignore_external_master' when copying slave connections. Whether it should do it
    // is questionable.
    // TODO: Think about what to do with this test and the setting in general.
    //    test.repl->replicate_from(1, 3);
    test.repl->replicate_from(3, 1);
    test.maxscale->wait_for_monitor(1);
    mxs.check_print_servers_status({down, master, slave});

    test.tprintf("Start server1, expect it to rejoin the cluster");
    // The rejoin should redirect the existing external master connection in server1.
    test.repl->start_node(0);
    test.maxscale->wait_for_monitor(2);
    mxs.check_print_servers_status({slave, master, slave});

    test.tprintf("Stop server2, expect server1 to be promoted as the master");
    test.repl->stop_node(1);
    test.maxscale->wait_for_monitor(2);
    test.repl->connect();
    // Same as before.
    // test.repl->replicate_from(0, 3);
    test.repl->replicate_from(3, 0);

    mxs.check_servers_status({master, down, slave});

    test.tprintf("Start server2, expect it to rejoin the cluster");
    test.repl->start_node(1);
    test.maxscale->wait_for_monitor(2);
    mxs.check_servers_status({master, slave, slave});

    // Cleanup
    is_running = false;
    thr.join();
    execute_query(test.repl->nodes[0], "STOP SLAVE; RESET SLAVE ALL;");
}
