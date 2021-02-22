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

static std::atomic<int> running {0};

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
            test.maxscales->readconn_slave() : test.maxscales->readconn_master();
        const char* type = counter % 2 == 0 ?
            "master_failure_mode=error_on_write" : "master_failure_mode=fail_on_write";

        conn.set_timeout(30);
        test.expect(conn.connect(), "Failed to connect to MaxScale: %s", conn.error());

        for (int i = 0; i < 100 && test.ok(); i++)
        {
            test.expect(conn.query("select repeat('a', 1000)"),
                        "Query failed (iteration %d, query %d) for %s: %s",
                        i, counter, type, conn.error());
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
    test.set_timeout(60);
    running = 2;

    for (auto& t : threads)
    {
        t.join();
    }

    return test.global_result;
}
