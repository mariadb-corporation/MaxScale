/**
 * Test MaxInfo with the SQL interface
 */

#include "testconnections.h"
#include <string>
#include <thread>
#include <vector>
#include <atomic>

using namespace std;

int main(int argc, char** argv)
{
    vector<string> commands(
    {
        "FLUSH LOGS",
        "SHOW VARIABLES",
        "SHOW VARIABLES LIKE '%version%'",
        "SHOW STATUS",
        "SHOW SERVICES",
        "SHOW LISTENERS",
        "SHOW SESSIONS",
        "SHOW CLIENTS",
        "SHOW SERVERS",
        "SHOW MODULES",
        "SHOW MONITORS",
        "SHOW EVENTTIMES"
    });

    TestConnections test(argc, argv);
    vector<thread> threads;
    atomic<bool> run(true);
    atomic<bool> wait(true);

    // Create some threads so that the SHOW SESSIONS will actually do something
    for (int i = 0; i < 25; i++)
    {
        threads.emplace_back([&]() {
                                 while (wait)
                                 {
                                     sleep(1);
                                 }

                                 while (run)
                                 {
                                     MYSQL* conn = test.maxscales->open_rwsplit_connection();
                                     for (int i = 0; i < 100; i++)
                                     {
                                         mysql_query(conn,
                                                     "SELECT REPEAT('a', 10000), sleep(0.01) FROM dual");
                                     }
                                     mysql_close(conn);
                                 }
                             });
    }

    wait = false;

    MYSQL* conn = test.maxscales->open_readconn_master_connection();

    for (int i = 0; i < 100; i++)
    {
        test.set_timeout(60);
        for (auto a : commands)
        {
            test.try_query(conn, "%s", a.c_str());
        }
    }

    test.stop_timeout();

    mysql_close(conn);

    run = false;

    test.set_timeout(60);

    for (auto& a : threads)
    {
        a.join();
    }

    test.stop_timeout();

    return test.global_result;
}
