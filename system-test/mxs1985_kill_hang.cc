/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

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
        threads.emplace_back([&] {
                                 while (running && test.global_result == 0)
                                 {
                                     MYSQL* c = test.maxscale->open_rwsplit_connection();

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
    test.reset_timeout();
    for_each(threads.begin(), threads.end(), mem_fn(&thread::join));
}

void mxs3251(TestConnections& test)
{
    running = true;
    vector<thread> threads;

    for (int i = 0; i < 20 && test.global_result == 0; i++)
    {
        threads.emplace_back(
            [&] {
                while (running && test.global_result == 0)
                {
                    MYSQL* c = test.maxscale->open_rwsplit_connection();
                    string query = "KILL " + to_string(mysql_thread_id(c));
                    execute_query_silent(c, query.c_str());
                    mysql_close(c);
                }
            });
    }

    sleep(10);
    running = false;

    // If MaxScale hangs, at least one thread will not return in time
    test.reset_timeout();
    for_each(threads.begin(), threads.end(), mem_fn(&thread::join));
}

void mxs4209(TestConnections& test)
{
    for (int i = 1; i <= 4; i++)
    {
        test.check_maxctrl("alter server server" + std::to_string(i) + " persistpoolmax 10");
        test.check_maxctrl("alter server server" + std::to_string(i) + " persistmaxtime 300s");
    }

    // Make sure there's connections in the pool
    std::vector<Connection> conns;

    for (int i = 0; i < 10; i++)
    {
        auto conn = test.maxscale->rwsplit();
        conn.connect();
        conn.query("SELECT 1");
        conns.push_back(std::move(conn));
    }

    conns.clear();

    test.check_maxctrl("enable log-priority info");

    MYSQL* conn = test.maxscale->open_rwsplit_connection();
    test.expect(conn, "First connection failed");
    auto other = test.maxscale->rwsplit();
    other.set_timeout(10);
    test.expect(other.connect(), "Second connection failed: %s", other.error());

    int id = mysql_thread_id(conn);
    const std::string kill = "KILL QUERY " + std::to_string(id);

    for (int i = 0; i < 10 && test.ok(); i++)
    {
        const std::string query = "SELECT SLEEP(30)";
        auto start = std::chrono::steady_clock::now();

        test.expect(mysql_send_query(conn, query.c_str(), query.size()) == 0,
                    "Query write failed for '%s': %s", query.c_str(), mysql_error(conn));
        sleep(1);

        test.expect(other.query(kill), "KILL failed: %s", other.error());

        mysql_read_query_result(conn);
        mysql_free_result(mysql_store_result(conn));

        auto end = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::duration<float>>(end - start);

        test.expect(diff < 10s, "Query took %f seconds when it should take less than 10 seconds",
                    diff.count());
    }

    mysql_close(conn);

    test.check_maxctrl("disable log-priority info");

    for (int i = 1; i <= 4; i++)
    {
        test.check_maxctrl("alter server server" + std::to_string(i) + " persistpoolmax 0");
        test.check_maxctrl("alter server server" + std::to_string(i) + " persistmaxtime 0s");
    }
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.log_printf("mxs1985");
    mxs1985(test);
    test.log_printf("mxs3251");
    mxs3251(test);
    test.log_printf("mxs4209");
    mxs4209(test);

    return test.global_result;
}
