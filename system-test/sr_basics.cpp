/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <map>
#include <vector>
#include <maxbase/assert.h>
#include <maxtest/testconnections.hh>

using namespace std;

namespace
{

void init(TestConnections& test, Connection& c)
{
    test.expect(c.query("DROP TABLE IF EXISTS sq"), "Could not drop table.");
    test.expect(c.query("CREATE TABLE sq (id INT, value INT)"), "Could not create table.");
    sleep(2);
}

void finish(TestConnections& test, Connection& c)
{
    test.expect(c.query("DROP TABLE IF EXISTS sq"), "Could not drop table.");
}

const size_t N_THREADS = 10;
const size_t N_INSERTS = 100;
const size_t N_SELECTS = 10;

void thread_stress(TestConnections* pTest, int id)
{
    string greeting("Hello from thread ");
    greeting += std::to_string(id);
    greeting += "\n";

    cout << greeting << flush;

    Connection c = pTest->maxscale->rwsplit();

    c.connect();

    string preamble("INSERT INTO sq VALUES (");
    preamble += std::to_string(id);
    preamble += ", ";

    for (size_t i = 0; i < N_INSERTS; ++i)
    {
        string query = preamble + std::to_string(i) + ")";

        c.query(query);

        for (size_t i = 0; i < N_SELECTS; ++i)
        {
            c.query("SELECT * FROM sq");
        }
    }

    string goodbye("Goodbye from thread ");
    goodbye += std::to_string(id);
    goodbye += "\n";

    cout << goodbye << flush;
}

void test_stress(TestConnections& test)
{
    vector<thread> threads;

    for (size_t i = 0; i < N_THREADS; ++i)
    {
        thread t(thread_stress, &test, i);

        threads.emplace_back(std::move(t));
    }

    for (size_t i = 0; i < threads.size(); ++i)
    {
        threads[i].join();
    }

    Connection c = test.maxscale->rwsplit();
    c.connect();

    test.repl->sync_slaves();

    Result rows = c.rows("SELECT * FROM sq");
    test.expect(rows.size() == N_THREADS * N_INSERTS,
                "Expected %lu inserts in total, but found %lu.", N_THREADS * N_INSERTS, rows.size());

    map<string, vector<string>> found_results;

    for (const auto& row : rows)
    {
        mxb_assert(row.size() == 2);

        string tid { row[0] };
        string f { row[1] };

        found_results[tid].push_back(f);
    }

    test.expect(found_results.size() == N_THREADS,
                "Expected results from %lu threads, but found %lu.", N_THREADS, found_results.size());

    for (const auto& kv : found_results)
    {
        const string& tid { kv.first };
        const vector<string>& fields { kv.second };

        test.expect(fields.size() == N_INSERTS,
                    "Expected %lu inserts for thread %s, but found only %lu.",
                    N_INSERTS, tid.c_str(), fields.size());
    }
}

void run_tests(TestConnections& test)
{
    test_stress(test);
}

}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    Connection c = test.maxscale->rwsplit();

    test.expect(c.connect(), "Could not connect to MaxScale.");

    init(test, c);

    run_tests(test);

    finish(test, c);

    return test.global_result;
}
