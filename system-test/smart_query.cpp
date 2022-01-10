/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/stopwatch.hh>
#include <iostream>
#include <vector>
#include <future>


/** Smart query test.
 *
 *  Since there is no columnstore support in the system-test yet, this code
 *  makes sure that when a query has been executed once, the subsequent runs
 *  of the same query are executed on the same server.
 *
 *  The setup is such that there are multiple ReadWriteSplits, each with a
 *  single server, where these RWS:s are the "servers" of the SmartRouter.
 *
 *  Caveat: This test assumes that the servers are (nearly) identical, which
 *          they are in system-test. If one server is much faster than the
 *          others this test might not find a problem even if there is one.
 */
namespace
{

constexpr int NUM_INTS = 1000;
constexpr int NUM_THREADS = 25;

// The test must finish before SmartRouter invalidates the cached entry (2 minutes).
const maxbase::Duration TEST_RUN_TIME = std::chrono::seconds(60);

/**
 * @brief setup two tables to be joined, enabling a query with only a little
 *        IO but sufficient server work to make a difference at the servers.
 * @param test
 */
void setup_test(TestConnections& test)
{
    Connection c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Could not connect to MaxScale.");
    test.expect(c.query("drop table if exists ints1"), "Could not drop ints1.");
    test.expect(c.query("drop table if exists ints2"), "Could not drop ints2.");
    test.expect(c.query("create table ints1(val int)"), "Could not create table ints1.");
    test.expect(c.query("create table ints2(val int)"), "Could not create table ints2.");

    std::string sequence = std::string("seq_1_to_") + std::to_string(NUM_INTS);

    test.expect(c.query("insert into ints1 select seq from " + sequence),
                "Could not insert into ints1.");
    test.expect(c.query("insert into ints2 select seq from " + sequence),
                "Could not insert into ints2.");

    test.repl->sync_slaves();
}

/**
 * @brief drop the tables
 * @param test
 */
void tear_down_test(TestConnections& test)
{
    Connection c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Could not connect to MaxScale.");

    test.expect(c.query("drop table if exists ints1"), "Could not drop ints1.");
    test.expect(c.query("drop table if exists ints2"), "Could not drop ints2.");
}

/**
 * @brief the one and only query. This should require enough work by the
 *        servers to make it unpredictable which one will finish first.
 */
const std::string THE_QUERY = "select @@server_id, count(*)"
                              " from ints1, ints2 where ints1.val = ints2.val";

/**
 * @brief execute the_query on a new connection
 * @param test
 * @return the server that executed the query
 */
int track_server(TestConnections& test)
{
    Connection c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Could not connect to MaxScale.");

    Result rows = c.rows(THE_QUERY);
    test.expect(rows.size() == 1, "Expected exactly one row.");

    int server_id = std::stoi(rows[0][0]);
    int count = std::stoi(rows[0][1]);

    test.expect(count == NUM_INTS, "Expected a count of %d, but got %d.", NUM_INTS, count);

    return server_id;
}

/**
 * @brief First execute the query once to establish which server the
 *        smart-router selected. Then run the query in parallel expecting
 *        all queries to be executed by the selected server.
 * @param test
 */
void run_test(TestConnections& test)
{
    maxbase::StopWatch sw;
    int selected_server_id = track_server(test);
    int test_count = 0;

    std::cout << "selected_server_id " << selected_server_id << std::endl;

    while (!test.global_result && sw.split() < TEST_RUN_TIME)
    {
        std::vector<std::future<int>> futures;
        for (int i = 0; i < NUM_THREADS; ++i)
        {
            futures.emplace_back(std::async(std::launch::async, track_server, std::ref(test)));
        }

        for (auto& fut : futures)
        {
            ++test_count;
            int server_id = fut.get();
            test.expect(selected_server_id == server_id,
                        "Expected %d server_id but got %d.", selected_server_id, server_id);
        }
    }

    std::cout << "number of tests run: " << test_count << std::endl;
}
}


int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    setup_test(test);
    run_test(test);
    tear_down_test(test);

    return test.global_result;
}
