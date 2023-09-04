/**
 * - create load on Master RWSplit
 * - block and unblock Master in the loop
 * - check logs for lack of errors "authentication failure", "handshake failure"
 * - check for lack of crashes in the log
 */

#include <maxtest/testconnections.hh>

std::atomic<bool> running {true};

void do_work(TestConnections& test)
{
    auto conn = test.maxscale->rwsplit();

    while (running)
    {
        conn.set_timeout(10);

        if (conn.connect())
        {
            conn.query("SELECT SLEEP(1), @@last_insert_id");
        }
        else
        {
            sleep(1);
        }
    }
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("Create query load");
    std::vector<std::thread> threads;

    /* Create independent threads each of them will create some load on Master */
    for (int i = 0; i < 10; i++)
    {
        threads.emplace_back(do_work, std::ref(test));
    }

    int iterations = 5;
    int sleep_interval = 5;

    for (int i = 0; i < iterations; i++)
    {
        sleep(sleep_interval);

        test.reset_timeout();
        test.log_printf("Block master");
        test.repl->block_node(0);

        sleep(sleep_interval);

        test.reset_timeout();
        test.log_printf("Unblock master");
        test.repl->unblock_node(0);
    }

    running = false;

    test.tprintf("Waiting for all master load threads exit");
    for (auto& thr : threads)
    {
        thr.join();
    }

    test.log_printf("Check that replication works");
    auto& mxs = *test.maxscale;
    mxs.wait_for_monitor(2);
    mxs.check_servers_status({mxt::ServerInfo::master_st, mxt::ServerInfo::slave_st});
    if (!test.ok())
    {
        return test.global_result;
    }

    test.check_maxscale_alive();
    test.log_excludes("due to authentication failure");
    test.log_excludes("due to handshake failure");
    test.log_excludes("Refresh rate limit exceeded for load of users' table");

    return test.global_result;
}
