/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

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

    for (int i = 0; i < iterations; i++)
    {
        test.reset_timeout();
        test.log_printf("Block master");
        test.repl->block_node(0);
        test.maxscale->wait_for_monitor();

        test.reset_timeout();
        test.log_printf("Unblock master");
        test.repl->unblock_node(0);
        test.maxscale->wait_for_monitor();
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
