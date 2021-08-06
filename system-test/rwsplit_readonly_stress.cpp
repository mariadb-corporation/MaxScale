/**
 * @file rwsplit_readonly.cpp Test of the read-only mode for readwritesplit when master fails with load
 * - start query threads which does SELECTs in the loop
 * - every 10 seconds block Master and then after another 10 seconds unblock master
 */

#include <atomic>
#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <thread>
#include <maxtest/testconnections.hh>

#define THREADS 16
using Clock = std::chrono::steady_clock;

static std::atomic<int> running {0};

int64_t diff_to_ms(Clock::time_point t)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

void query_thread(TestConnections& test)
{
    int counter = 0;

    while (running.load() == 0)
    {
        sleep(1);
    }

    while (running.load() == 1 && test.ok())
    {
        auto conn = counter % 2 == 0 ?
            test.maxscale->readconn_slave() : test.maxscale->readconn_master();
        const char* type = counter % 2 == 0 ?
            "master_failure_mode=error_on_write" : "master_failure_mode=fail_on_write";

        conn.set_timeout(30);
        test.expect(conn.connect(), "Failed to connect to MaxScale: %s", conn.error());

        for (int i = 0; i < 100 && test.ok(); i++)
        {
            auto start = Clock::now();
            test.expect(conn.query("select repeat('a', 1000)"),
                        "Query failed (iteration %d, query %d) for %s, waited for %lums, thread ID %u: %s",
                        i, counter, type, diff_to_ms(start), conn.thread_id(), conn.error());
        }

        ++counter;
    }
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    std::vector<std::thread> threads;

    for (int i = 0; i < THREADS; i++)
    {
        threads.emplace_back(query_thread, std::ref(test));
    }

    running = 1;

    for (int i = 0; i < 5 && test.ok(); i++)
    {
        test.tprintf("Blocking master");
        test.repl->block_node(0);
        sleep(10);

        test.tprintf("Unblocking master");
        test.repl->unblock_node(0);
        sleep(10);
    }

    test.tprintf("Waiting for all threads to finish\n");
    test.reset_timeout();
    running = 2;

    for (auto& t : threads)
    {
        t.join();
    }

    return test.global_result;
}
