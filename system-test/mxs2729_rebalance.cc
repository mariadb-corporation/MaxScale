/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
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

struct ThreadInfo
{
    ThreadInfo()
    {}

    ThreadInfo(int nConnections, int nLoad)
       : nConnections(nConnections)
       , nLoad(nLoad)
    {
    }

    int nConnections = 0;
    int nLoad = 0;
};

ostream& operator << (ostream& out, const ThreadInfo& ti)
{
    out << "load=" << ti.nLoad << ", connections=" << ti.nConnections;
    return out;
}

ostream& operator << (ostream& out, const std::map<int, ThreadInfo>& m)
{
    for (auto kv : m)
    {
        out << kv.first << ": " << kv.second << endl;
    }
    return out;
}

map<int,ThreadInfo> get_thread_info(TestConnections& test)
{
    map<int,ThreadInfo> rv;
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
        json_t* pLoad = json_object_get(pStats, "load");
        json_t* pLoad_second = json_object_get(pLoad, "last_second");

        const char* zId = json_string_value(pId);
        int current_descriptors = json_integer_value(pCurrent_descriptors);
        int load = json_integer_value(pLoad_second);

        rv.emplace(atoi(zId), ThreadInfo(current_descriptors, load));
    }

    json_decref(pJson);

    return rv;
}

void move_connections_to_thread(TestConnections& test,
                                int tid,
                                const map<int,ThreadInfo>& connections_by_thread)
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

            auto result = test.maxscale->ssh_output(curl);

            cout << result.output << endl;
        }
    }
}

void start_rebalancing(TestConnections& test, int rebalance_period, int rebalance_threshold)
{
    std::ostringstream ss;

    ss << "alter maxscale"
       << " rebalance_window " << rebalance_period * 2
       << " rebalance_threshold " << rebalance_threshold
       << " rebalance_period " << rebalance_period << "s";

    test.check_maxctrl(ss.str());
}
}

void run(TestConnections* pTest, mxb::Semaphore* pSem_ready, mxb::Semaphore* pSem_exit)
{
    TestConnections& test = *pTest;
    mxb::Semaphore& sem_ready = *pSem_ready;
    mxb::Semaphore& sem_exit = *pSem_exit;

    Connection c = test.maxscale->rwsplit();
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
    map<int,ThreadInfo> cbt1 = get_thread_info(test);
    cout << "Connection distribution at startup:\n" << cbt1 << endl;

    int nMaxscale_threads = cbt1.size();

    int nConn_total1 = 0;

    for (auto kv : cbt1)
    {
        nConn_total1 += kv.second.nConnections;
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

    map<int,ThreadInfo> cbt2 = get_thread_info(test);
    cout << "Connection distribution after thread start:\n" << cbt2 << endl;
    mxb_assert(cbt2.size() == cbt1.size());

    int nConn_total2 = 0;

    for (auto kv : cbt2)
    {
        nConn_total2 += kv.second.nConnections;
    }

    int nConn_per_session = (nConn_total2 - nConn_total1) / nThreads;

    move_connections_to_thread(test, 0, cbt2);
    sleep(2); // To allow some time for the explicit moving to have time to finish.

    map<int,ThreadInfo> cbt3 = get_thread_info(test);
    cout << "Connection distribution after explicit rebalance to thread 0:\n" << cbt3 << endl;
    mxb_assert(cbt3.size() == cbt2.size());

    auto it1 = cbt1.begin();
    auto it3 = cbt3.begin();

    while (it1 != cbt1.end())
    {
        int wid = it1->first;

        if (wid != 0)
        {
            int conns1 = it1->second.nConnections;
            int conns2 = it3->second.nConnections;

            test.expect(conns1 == conns2,
                        "Rebalance did not move all connections from thread %d.", wid);
        }

        ++it1;
        ++it3;
    }

    int nConn_max = cbt3[0].nConnections;
    int nConn_to_move = (nMaxscale_threads - 1) * (nConn_max - nConn_default) / nMaxscale_threads;
    int nMax_rounds = nConn_to_move / nConn_per_session; // Should be worst case.

    int rebalance_period = 1;
    int rebalance_threshold = 10;
    start_rebalancing(test, rebalance_period, rebalance_threshold);

    int n = 1;
    bool rebalanced = false;
    while (!rebalanced && (n <= nMax_rounds))
    {
        sleep(rebalance_period * 2);

        map<int,ThreadInfo> cbt4 = get_thread_info(test);

        int avg = 0;
        int min = std::numeric_limits<int>::max();
        int max = std::numeric_limits<int>::min();

        for (auto kv : cbt4)
        {
            avg += kv.second.nLoad;

            if (kv.second.nLoad > max)
            {
                max = kv.second.nLoad;
            }

            if (kv.second.nLoad < min)
            {
                min = kv.second.nLoad;
            }
        }

        avg /= cbt4.size();

        cout << "Rebalancing (" << n << "):\n" << cbt4 << endl;
        cout << "Avg: " << avg << endl;
        cout << "Min: " << min << endl;
        cout << "Max: " << max << endl;

        // We are happy when the difference between min and max is what we requested
        // in the rebalance command.
        rebalanced = (max - min <= rebalance_threshold);

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
