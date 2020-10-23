/**
 * MXS-1985: MaxScale hangs on concurrent KILL processing
 *
 * Regression test for the following bugs:
 *   MXS-1985
 *   MXS-3251
 */

#include <maxtest/testconnections.hh>

#include <atomic>
#include <thread>
#include <iostream>

using namespace std;

static atomic<bool> running {true};

void mxs1985(TestConnections& test)
{
    running = true;
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
    for_each(threads.begin(), threads.end(), mem_fn(&thread::join));
}

void mxs3251(TestConnections& test)
{
    running = true;
    vector<thread> threads;

    for (int i = 0; i < 20 && test.global_result == 0; i++)
    {
        threads.emplace_back(
            [&, i]() {
                while (running && test.global_result == 0)
                {
                    MYSQL* c = test.maxscales->open_rwsplit_connection();
                    string query = "KILL " + to_string(mysql_thread_id(c));
                    execute_query_silent(c, query.c_str());
                    mysql_close(c);
                }
            });
    }

    sleep(10);
    running = false;

    // If MaxScale hangs, at least one thread will not return in time
    test.set_timeout(30);
    for_each(threads.begin(), threads.end(), mem_fn(&thread::join));
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    mxs1985(test);
    mxs3251(test);

    return test.global_result;
}
