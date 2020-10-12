/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-09-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <jansson.h>
#include <map>
#include <iostream>
#include <sstream>
#include <maxbase/log.hh>
#include <maxbase/semaphore.hh>
#include <maxtest/testconnections.hh>
#include <maxtest/mariadb_func.hh>

using namespace std;

namespace
{

ostream& operator << (ostream& out, const std::map<int, int>& m)
{
    for (auto kv : m)
    {
        out << kv.first << ": " << kv.second << endl;
    }
    return out;
}

map<int,int> get_thread_connections(TestConnections& test)
{
    map<int,int> rv;
    auto result = test.maxctrl("api get maxscale/threads");
    mxb_assert(result.rc == 0);

    json_error_t error;
    json_t* pJson = json_loads(result.output.c_str(), 0, &error);

    json_t* pDatas = json_object_get(pJson, "data");

    size_t n = json_array_size(pDatas);
    for (size_t i = 0; i < n; ++i)
    {
        json_t* pData = json_array_get(pDatas, i);
        json_t* pId = json_object_get(pData, "id");
        json_t* pAttributes = json_object_get(pData, "attributes");
        json_t* pStats = json_object_get(pAttributes, "stats");
        json_t* pCurrent_descriptors = json_object_get(pStats, "current_descriptors");

        const char* zId = json_string_value(pId);
        int current_descriptors = json_integer_value(pCurrent_descriptors);

        rv.emplace(atoi(zId), current_descriptors);
    }

    return rv;
}

void move_connections_to_thread(TestConnections& test, int tid, const map<int,int>& connections_by_thread)
{
    for (auto kv : connections_by_thread)
    {
        if (kv.first != tid)
        {
            string curl { "curl -u admin:mariadb -X POST http://127.0.0.1:8989/v1/maxscale/threads/" };

            curl += std::to_string(kv.first);
            curl += "/rebalance?";
            curl += "recipient=" + std::to_string(tid);

            cout << curl << endl;

            auto result = test.maxscales->ssh_output(curl);

            cout << result.output << endl;
        }
    }
}

void start_rebalancing(TestConnections& test, int rebalance_period)
{
    test.maxctrl("alter maxscale rebalance_window 2");
    test.maxctrl("alter maxscale rebalance_threshold 10");

    string cmd { "alter maxscale rebalance_period " };
    cmd += std::to_string(rebalance_period);
    cmd += "s";

    test.maxctrl(cmd);
}

}

void run(TestConnections* pTest, mxb::Semaphore* pSem_ready, mxb::Semaphore* pSem_exit)
{
    TestConnections& test = *pTest;
    mxb::Semaphore& sem_ready = *pSem_ready;
    mxb::Semaphore& sem_exit = *pSem_exit;

    Connection c = test.maxscales->rwsplit();
    bool connected = c.connect();
    test.expect(connected, "Could not connect to MaxScale.");

    if (!connected)
    {
        exit(test.global_result);
    }

    sem_ready.post();

    while (!sem_exit.trywait())
    {
        c.query("SELECT 1");
    }
}

int main(int argc, char* argv[])
{
    mxb::Log log;
    TestConnections test(argc, argv);

    // cbt = connections by thread
    map<int,int> cbt1 = get_thread_connections(test);
    cout << "Connection distribution at startup:\n" << cbt1 << endl;

    int nMaxscale_threads = cbt1.size();

    int nConn_total1 = 0;

    for (auto kv : cbt1)
    {
        nConn_total1 += kv.second;
    }

    // This is as many connections a thread will have by default after startup.
    int nConn_default = nConn_total1 / nMaxscale_threads;

    int nThreads = 30;

    vector<thread> threads;

    mxb::Semaphore sem_ready;
    mxb::Semaphore sem_exit;

    for (int i = 0; i < nThreads; ++i)
    {
        threads.emplace_back(run, &test, &sem_ready, &sem_exit);
    }

    cout << "Threads started." << endl;

    sem_ready.wait_n(nThreads);

    cout << "Threads ready." << endl;

    map<int,int> cbt2 = get_thread_connections(test);
    cout << "Connection distribution after thread start:\n" << cbt2 << endl;
    mxb_assert(cbt2.size() == cbt1.size());

    int nConn_total2 = 0;

    for (auto kv : cbt2)
    {
        nConn_total2 += kv.second;
    }

    int nConn_per_session = (nConn_total2 - nConn_total1) / nThreads;

    move_connections_to_thread(test, 0, cbt2);
    sleep(2); // To allow some time for the explicit moving to have time to finish.

    map<int,int> cbt3 = get_thread_connections(test);
    cout << "Connection distribution after explicit rebalance to thread 0:\n" << cbt3 << endl;
    mxb_assert(cbt3.size() == cbt2.size());

    auto it1 = cbt1.begin();
    auto it3 = cbt3.begin();

    while (it1 != cbt1.end())
    {
        int wid = it1->first;

        if (wid != 0)
        {
            int conns1 = it1->second;
            int conns2 = it3->second;

            test.expect(conns1 == conns2,
                        "Rebalance did not move all connections from thread %d.", wid);
        }

        ++it1;
        ++it3;
    }

    int nConn_max = cbt3[0];
    int nConn_to_move = (nMaxscale_threads - 1) * (nConn_max - nConn_default) / nMaxscale_threads;
    int nSessions_to_move = nConn_to_move / nConn_per_session;
    int nMax_rounds = nSessions_to_move; // Should be worst case.

    int rebalance_period = 1;
    start_rebalancing(test, rebalance_period);

    int n = 1;
    bool rebalanced = false;
    while (!rebalanced && (n <= nMax_rounds))
    {
        sleep(rebalance_period * 2);

        map<int,int> cbt4 = get_thread_connections(test);

        int avg = 0;
        int min = std::numeric_limits<int>::max();
        int max = std::numeric_limits<int>::min();

        for (auto kv : cbt4)
        {
            avg += kv.second;

            if (kv.second > max)
            {
                max = kv.second;
            }

            if (kv.second < min)
            {
                min = kv.second;
            }
        }

        avg /= cbt4.size();

        cout << "Rebalancing (" << n << "):\n" << cbt4 << endl;
        cout << "Avg: " << avg << endl;
        cout << "Min: " << min << endl;
        cout << "Max: " << max << endl;

        // We are happy when the difference between min/max and avg corresponds to
        // the #connections of a session.
        rebalanced = (avg - min <= nConn_per_session) && (max - avg <= nConn_per_session);

        if (!rebalanced)
        {
            ++n;
        }

        cout << "----------" << endl;
    }

    if (rebalanced)
    {
        cout << "Rebalanced after " << n << " rounds." << endl;
    }

    test.expect(rebalanced, "Threads were not rebalanced after %d rounds.", n - 1);

    sem_exit.post_n(nThreads);

    for (int i = 0; i < nThreads; ++i)
    {
        threads[i].join();
    }

    cout << "Threads joined." << endl;

    return test.global_result;
}
