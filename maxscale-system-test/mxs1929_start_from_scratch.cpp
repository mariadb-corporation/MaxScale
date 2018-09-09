/**
 * MXS-1929: Create a setup from an empty config and check that it can be
 * repeated multiple times
 */

#include "testconnections.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    std::vector<std::thread> threads;
    std::atomic<bool> running {true};
    std::atomic<int> conns {0};

    auto start = std::chrono::steady_clock::now();

    // Create some threads so that we have a constant load on the system
    for (int i = 0; i < 10; i++)
    {
        threads.emplace_back([&, i]() {
                                 while (running)
                                 {
                                     Connection c = i % 2 == 0 ? test.maxscales->rwsplit() : test.maxscales->readconn_master();
                                     if (c.connect())
                                     {
                                         c.query("CREATE TABLE IF NOT EXITS test.t1 (id INT)");
                                         c.query("INSERT INTO test.t1 VALUES (" + std::to_string(i) + ")");
                                         c.query("SELECT * FROM test.t1");
                                         c.query("DELETE FROM test.t1 WHERE id = " + std::to_string(i));
                                         ++conns;
                                     }
                                     else
                                     {
                                         sleep(1);
                                     }
                                 }
                             });
    }

    // Allows the use of operator+ for concatenation
    std::string create = "create ";

    std::vector<std::string> commands
    {
        // Start by creating the servers
        create + "server server1 " + test.repl->IP[0] + " 3306",
        create + "server server2 " + test.repl->IP[1] + " 3306",
        create + "server server3 " + test.repl->IP[2] + " 3306",
        create + "server server4 " + test.repl->IP[3] + " 3306",
        // A monitor for the servers
        create
        +
        "monitor monitor1 mysqlmon monitor_interval=1000 user=skysql password=skysql --servers server1 server2 server3 server4",
        // Services, one readwritesplit and one readconnroute
        create
        +
        "service service1 readwritesplit user=skysql password=skysql --servers server1 server2 server3 server4",
        create
        +
        "service service2 readconnroute user=skysql password=skysql router_options=master --servers server1 server2 server3 server4",
        // Create listeners for the services
        create + "listener service1 listener1 4006",
        create + "listener service2 listener2 4008",
        // Create the filters
        create + "filter filter1 qlafilter filebase=/tmp/qla",
        create + "filter filter2 regexfilter match=hello replace=world",
        // Take filters into use
        "alter service-filters service1 filter1",
        "alter service-filters service2 filter2",
        // Remove server then filters
        "alter service-filters service1",
        "unlink service service1 server1 server2 server3 server4",
        // Do it the other way around for the second service
        "unlink service service2 server1 server2 server3 server4",
        "alter service-filters service2",
        // Unlink the monitor from the servers
        "unlink monitor monitor1 server1 server2 server3 server4",
        // Start destroying things
        "destroy filter filter1",
        "destroy filter filter2",
        "destroy listener service1 listener1",
        "destroy listener service2 listener2",
        // Draining the servers makes sure they aren't used
        "drain server server1",
        "drain server server2",
        "drain server server3",
        "drain server server4",
        "destroy service service1",
        "destroy service service2",
        "destroy monitor monitor1",
        "destroy server server1",
        "destroy server server2",
        "destroy server server3",
        "destroy server server4"
    };

    for (int i = 0; i < 3; i++)
    {
        for (const auto& cmd : commands)
        {
            test.set_timeout(60);
            test.check_maxctrl(cmd);
        }

        test.tprintf("Completed round %d", i + 1);
    }

    running = false;

    for (auto& a : threads)
    {
        test.set_timeout(60);
        a.join();
    }

    auto end = std::chrono::steady_clock::now();

    test.tprintf("A total of %d connections were created over %d seconds",
                 conns.load(),
                 std::chrono::duration_cast<std::chrono::seconds>(end - start).count());

    return test.global_result;
}
