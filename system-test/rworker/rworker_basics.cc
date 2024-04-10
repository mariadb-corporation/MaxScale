/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxscale/routingworker.hh>
#include <maxtest/maxrest.hh>
#include <maxtest/testconnections.hh>

using namespace std;

#define ENTER_TEST() do {cout << __func__ << endl;} while (false)

namespace
{

void make_deaf(MaxRest& maxrest, const string& id)
{
    ostringstream path;
    path << "maxscale/debug/threads/" << id << "/unlisten";

    maxrest.curl_put(path.str());
}

void make_deaf(MaxRest& maxrest, int id)
{
    make_deaf(maxrest, std::to_string(id));
}

void make_deaf(MaxRest& maxrest, const MaxRest::Thread& thread)
{
    make_deaf(maxrest, thread.id);
}

void make_listening(MaxRest& maxrest, const string& id)
{
    ostringstream path;
    path << "maxscale/debug/threads/" << id << "/listen";

    maxrest.curl_put(path.str());
}

void make_listening(MaxRest& maxrest, int id)
{
    make_listening(maxrest, std::to_string(id));
}

void make_listening(MaxRest& maxrest, const MaxRest::Thread& thread)
{
    make_listening(maxrest, thread.id);
}

template<class T, class S>
void check_value(TestConnections& test, const T& t, S(T::* pMember), S s)
{
    if ((t.*pMember) != s)
    {
        ostringstream ss;
        ss << "Expected '" << s << "', but found '" << (t.*pMember) << "'" << endl;
        test.expect(false, "%s", ss.str().c_str());
    }
}

template<class T, class S>
void check_value(TestConnections& test,
                 typename vector<T>::const_iterator it,
                 typename vector<T>::const_iterator end,
                 S(T::* pMember),
                 S s)
{
    while (it != end)
    {
        check_value(test, *it, pMember, s);
        ++it;
    }
}

template<class T, class S>
void check_value(TestConnections& test, const vector<T>& states, S(T::* pMember), S s)
{
    check_value(test, states.begin(), states.end(), pMember, s);
}

int random_percent()
{
    double d = random();

    return 100 * (d / RAND_MAX);
}

bool dump_states(const vector<MaxRest::Thread>& threads, const string& expected = string())
{
    bool rv = true;

    for (const auto& t : threads)
    {
        if (!expected.empty() && (t.state != expected))
        {
            rv = false;
        }

        if (t.state == "Active")
        {
            cout << "A";
        }
        else if (t.state == "Draining")
        {
            cout << "G";
        }
        else if (t.state == "Dormant")
        {
            cout << "D";
        }
        else
        {
            mxb_assert(!true);
        }

        cout << " ";
    }

    cout << endl;

    return rv;
}

void sleep_enough(int from_workers, int to_workers)
{
    if (to_workers >= from_workers)
    {
        sleep(1);
    }
    else
    {
        int diff = from_workers - to_workers;
        std::chrono::seconds delay = (diff + 1) * mxs::RoutingWorker::TERMINATION_DELAY;

        sleep(delay.count());
    }
}

void wait_until_not_terminating(MaxRest& maxrest)
{
    string url {"maxscale/debug/termination_in_process"};
    bool tim;

    do
    {
        auto json = maxrest.curl_get("maxscale/debug/termination_in_process");
        MXB_AT_DEBUG(bool rv = ) json.try_get_bool("termination_in_process", &tim);
        mxb_assert(rv);
    }
    while (tim);
}
}

//
// smoke_test1
//
// Increase and decrease workers with no clients.
//
void smoke_test1(TestConnections& test, MaxRest& maxrest)
{
    ENTER_TEST();

    vector<MaxRest::Thread> threads;

    // Assume 4 initial threads.
    threads = maxrest.show_threads();
    test.expect(threads.size() == 4, "1: Expected 4 initial threads, but found %d.", (int)threads.size());

    maxrest.alter_maxscale("threads", 8);
    sleep_enough(4, 8);

    threads = maxrest.show_threads();
    test.expect(threads.size() == 8, "2: Expected 8 threads, but found %d.", (int)threads.size());

    maxrest.alter_maxscale("threads", 4);
    sleep_enough(8, 4);

    threads = maxrest.show_threads();
    test.expect(threads.size() == 4, "3: Expected 4 threads, but found %d.", (int)threads.size());
}

//
// smoke_test2
//
//
// Set #threads to an invalid number
//
void smoke_test2(TestConnections& test, MaxRest& maxrest)
{
    ENTER_TEST();

    vector<MaxRest::Thread> threads;

    // Assume 4 initial threads.
    threads = maxrest.show_threads();
    test.expect(threads.size() == 4, "Expected 4 initial threads, but found %d.", (int)threads.size());

    // A failure now means that things work as expected.
    maxrest.fail_on_error(false);

    try
    {
        maxrest.alter_maxscale("threads", 0);
        test.expect(false, "Setting the threads to 0 succeeded.");
    }
    catch (const std::exception& x)
    {
        cerr << "Exception: " << x.what() << endl;
    }

    try
    {
        maxrest.alter_maxscale("threads", 1024);
        test.expect(false, "Setting the threads to 1024 succeeded.");
    }
    catch (const std::exception& x)
    {
        cerr << "Exception: " << x.what() << endl;
    }

    maxrest.fail_on_error(true);
}

//
// smoke_test3
//
//
// Turn listening on and off.
//
void smoke_test3(TestConnections& test, MaxRest& maxrest)
{
    ENTER_TEST();

    vector<MaxRest::Thread> threads;

    threads = maxrest.show_threads();
    test.expect(threads.size() == 4, "Expected 4 initial threads, but found %d.", (int)threads.size());

    // Make them all deaf.
    for (const auto& t : threads)
    {
        make_deaf(maxrest, t);
    }

    threads = maxrest.show_threads();

    // Check that they indeed are deaf.
    for (const auto& t : threads)
    {
        test.expect(!t.listening, "Expected worker %s to be deaf, but it wasn't.", t.id.c_str());
    }

    // Make them listening again.
    for (const auto& t : threads)
    {
        make_listening(maxrest, t);
    }

    threads = maxrest.show_threads();

    // Check that they indeed are listening.
    for (const auto& t : threads)
    {
        test.expect(t.listening, "Expected worker %s to be listening, but it wasn't.", t.id.c_str());
    }
}

//
// smoke_test4
//
//
// Decrease threads when living sessions.
//
void smoke_test4(TestConnections& test, MaxRest& maxrest)
{
    ENTER_TEST();

    vector<MaxRest::Thread> threads;

    threads = maxrest.show_threads();
    test.expect(threads.size() == 4, "0: Expected %d threads, found %d.", 4, (int)threads.size());

    // Initially make all workers deaf.
    for (int i = 0; i < 4; ++i)
    {
        make_deaf(maxrest, i);
    }

    vector<Connection> connections;
    for (int i = 0; i < 4; ++i)
    {
        connections.emplace_back(test.maxscale->ip(), 4006, "maxskysql", "skysql");
        // Make particular worker listening when connecting => each connection to different worker.
        make_listening(maxrest, i);
        test.expect(connections[i].connect(), "1: Could not connect to MaxScale.");
        make_deaf(maxrest, i);
    }

    // Make all workers listening.
    for (int i = 0; i < 4; ++i)
    {
        make_listening(maxrest, i);
    }

    // And now all workers are fully normal.
    threads = maxrest.show_threads();
    check_value(test, threads, &MaxRest::Thread::state, string("Active"));

    // Tuning the number of threads to 1; as they all have connections, none should disappear.
    maxrest.alter_maxscale("threads", 1);
    sleep_enough(4, 1);
    threads = maxrest.show_threads();
    test.expect(threads.size() == 4, "2: Expected 4 threads but found %d.", (int)threads.size());

    // The first thread should be "Active", but the rest "Draining" as those connections are still alive.
    check_value(test, threads[0], &MaxRest::Thread::state, string("Active"));
    check_value(test, ++threads.begin(), threads.end(), &MaxRest::Thread::state, string("Draining"));

    // Tuning the number of threads to 5.
    maxrest.alter_maxscale("threads", 5);
    sleep_enough(4, 5);
    threads = maxrest.show_threads();
    test.expect(threads.size() == 5, "3: Expected 5 threads but found %d.", (int)threads.size());

    // And they should all be active.
    check_value(test, threads, &MaxRest::Thread::state, string("Active"));

    // Tuning the number of threads to 1.
    maxrest.alter_maxscale("threads", 1);
    sleep_enough(5, 1);
    threads = maxrest.show_threads();
    // The fifth thread should go down, as there are no connections.
    test.expect(threads.size() == 4, "4: Expected 4 threads but found %d.", (int)threads.size());

    // The first thread should be "Active", but the rest "Draining" as those connections are still alive.
    check_value(test, threads[0], &MaxRest::Thread::state, string("Active"));
    check_value(test, ++threads.begin(), threads.end(), &MaxRest::Thread::state, string("Draining"));

    // Close all connections, the draining threads should become dormant.
    connections.clear();
    sleep_enough(4, 1);
    threads = maxrest.show_threads();
    test.expect(threads.size() == 1, "4: Expected 1 threads but found %d.", (int)threads.size());
}

//
// stress_test1
//
// - Create lots of workers.
// - Create lots of clients that
//   * connect,
//   * perform 10% updates, 90% selects in a loop for 5 seconds
//   * disconnect
//   in a loop.
// - Meanwhile decrease the workers until there is only 1 left
// - Increase the number of workers until we are back where we started.

namespace
{

void stress_test1_setup(TestConnections& test)
{
    Connection c(test.maxscale->ip(), 4006, "maxskysql", "skysql");

    test.expect(c.connect(), "Could not connect to MaxScale.");
    test.expect(c.query("CREATE TABLE IF NOT EXISTS test.rworker (f INT)"),
                "Could not CREATE test.rworker");
    test.expect(c.query("INSERT INTO test.rworker VALUES (1)"),
                "Could not INSERT to test.rworker");
}

void stress_test1_finish(TestConnections& test)
{
    Connection c(test.maxscale->ip(), 4006, "maxskysql", "skysql");

    test.expect(c.connect(), "Could not connect to MaxScale.");
    test.expect(c.query("DROP TABLE IF EXISTS test.rworker"),
                "Could not DROP test.rworker");
}

void stress_test1_client(TestConnections* pTest, bool* pTerminate, int i)
{
    TestConnections& test = *pTest;
    bool& terminate = *pTerminate;

    string update("UPDATE test.rworker SET f = ");
    update += std::to_string(i);

    while (!terminate)
    {
        Connection c(test.maxscale->ip(), 4006, "maxskysql", "skysql");

        if (c.connect())
        {
            auto start = time(nullptr);

            while (!terminate && (time(nullptr) - start < 5))
            {
                if (random_percent() <= 10)
                {
                    if (!c.query(update))
                    {
                        test.expect(false, "Could not UPDATE: %s", c.error());
                        terminate = true;
                        return;
                    }
                }
                else
                {
                    if (!c.query("SELECT * FROM test.rworker"))
                    {
                        test.expect(false, "Could not SELECT: %s", c.error());
                        terminate = true;
                        return;
                    }
                }
            }
        }
        else
        {
            test.expect(false, "Could not connect.");
            return;
        }
    }
}
}

void stress_test1(TestConnections& test, MaxRest& maxrest)
{
    ENTER_TEST();

    stress_test1_setup(test);

    const int64_t nWorkers = 13;
    const int nClients = 17;

    maxrest.alter_maxscale("threads", nWorkers);
    sleep(1);

    vector<std::thread> client_threads;
    bool terminate = false;

    try
    {
        for (int i = 0; i < nClients; ++i)
        {
            client_threads.emplace_back(&stress_test1_client, &test, &terminate, i);
        }

        sleep(1);

        auto threads = maxrest.show_threads();
        dump_states(threads);

        for (int i = nWorkers - 1; i > 0; --i)
        {
            if (terminate)
            {
                break;
            }

            maxrest.alter_maxscale("threads", i);

            threads = maxrest.show_threads();

            while (!dump_states(threads, "Active"))
            {
                sleep(1);
                threads = maxrest.show_threads();
            }

            // When the while-loop ends, all threads are active, i.e. the Draining one
            // has been drained and the termination has commenced. Thus, we need to wait
            // until the termination has finished, before proceeding.

            wait_until_not_terminating(maxrest);
        }

        threads = maxrest.show_threads();

        int nWaits = 5;
        while (threads.size() != 1 && nWaits != 0)
        {
            sleep(1);
            threads = maxrest.show_threads();
            if (threads.size() != 1)
            {
                cout << "The number of workers not yet dropped to 1, sleeping and re-checking" << endl;
                sleep(1);
                --nWaits;
            }
        }

        test.expect(threads.size() == 1, "Unexpected number of threads: %d", (int)threads.size());

        for (int i = 2; i <= nWorkers; ++i)
        {
            if (terminate)
            {
                break;
            }

            maxrest.alter_maxscale("threads", i);

            for (int j = 0; j < 5; ++j)
            {
                threads = maxrest.show_threads();
                dump_states(threads);
                sleep(1);
            }
        }

        threads = maxrest.show_threads();
        test.expect(threads.size() == nWorkers, "Unexpected number of threads: %d", (int)threads.size());
    }
    catch (const exception& x)
    {
        cerr << "Exception: " << x.what() << endl;
    }

    terminate = true;

    for (size_t i = 0; i < client_threads.size(); ++i)
    {
        client_threads[i].join();
    }

    cout << endl;

    stress_test1_finish(test);
}

void test_main(TestConnections& test)
{
    MaxRest maxrest(&test);

    try
    {
        smoke_test1(test, maxrest);
        smoke_test2(test, maxrest);
        smoke_test3(test, maxrest);
        smoke_test4(test, maxrest);

        stress_test1(test, maxrest);

        maxrest.alter_maxscale("threads", 4);
    }
    catch (const std::exception& x)
    {
        cerr << "Exception: " << x.what() << endl;
    }
}

int main(int argc, char* argv[])
{
    mxb::Log log;
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}
