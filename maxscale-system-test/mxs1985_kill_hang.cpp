/**
 * MXS-1985: MaxScale hangs on concurrent KILL processing
 */

#include "testconnections.h"

#include <atomic>
#include <thread>
#include <iostream>

using namespace std;

static atomic<bool> running {true};

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    vector<thread> threads;

    for (int i = 0; i < 20 && test.global_result == 0; i++)
    {
        threads.emplace_back([&, i]() {
                                 while (running && test.global_result == 0)
                                 {
                                     MYSQL* c = test.maxscales->open_rwsplit_connection();

                                    // It doesn't really matter if the connection ID exists, this is just a
                                    // handy way of generating cross-thread communication.
                                     for (auto&& a : get_result(c,
                                                                "SELECT id FROM information_schema.processlist"
                                                                " WHERE user like '%skysql%'"))
                                     {
                                         if (execute_query_silent(c, std::string("KILL " + a[0]).c_str()))
                                         {
                                             break;
                                         }
                                     }

                                     mysql_close(c);
                                 }
                             });
    }

    sleep(10);
    running = false;

    // If MaxScale hangs, at least one thread will not return in time
    test.set_timeout(30);
    for (auto&& a : threads)
    {
        a.join();
    }

    return test.global_result;
}
