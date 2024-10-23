/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/string.hh>

constexpr int NUM_THREADS = 100;
constexpr int NUM_SWITCHOVERS = 10;
constexpr int NUM_UPDATES = 5;
std::array<std::atomic<uint64_t>, NUM_THREADS> counters;
std::atomic<bool> running {true};

void query_thread(TestConnections& test, int my_id)
{
    auto c = test.maxscale->rwsplit();
    c.set_credentials("mxs5268", "mxs5268");
    test.expect(c.connect(), "Failed to connect: %s", c.error());
    uint32_t thread_id = c.thread_id();

    // TODO: Configure transaction_replay_safe_commit=false in newer versions
    std::vector<std::string> queries;
    queries.push_back("BEGIN");

    for (int i = 0; i < NUM_UPDATES; i++)
    {
        queries.push_back("UPDATE t1 SET val = val + 1 WHERE id = " + std::to_string(my_id));
    }

    queries.push_back("COMMIT");

    while (running && test.ok())
    {
        for (const auto& sql : queries)
        {
            if (!test.expect(c.query(sql), "(%u) %s failed: %s", thread_id, sql.c_str(), c.error()))
            {
                return;
            }
        }

        counters[my_id]++;
    }
}

std::vector<uint64_t> get_counters(size_t n)
{
    std::vector<uint64_t> rval;

    for (size_t i = 0; i < n; i++)
    {
        rval.push_back(counters[i].load());
    }

    return rval;
}

void wait_for_progress(TestConnections& test, const std::vector<uint64_t>& old)
{
    for (size_t i = 0; i < old.size(); i++)
    {
        while (test.ok() && old[i] == counters[i].load())
        {
            std::this_thread::sleep_for(1ms);
        }
    }
}

void print_status(TestConnections& test, const std::string& current)
{
    std::string symbols;
    test.maxscale->wait_for_monitor();
    auto servers = test.maxscale->get_servers();

    for (size_t i = 0; i < servers.size(); i++)
    {
        auto s = servers.get(i);
        symbols += s.is_master() ? " [*]" : s.is_slave() ? " [ ]" : " [!]";
    }

    test.log_printf("Switchover to '%s':\t%s", current.c_str(), symbols.c_str());
}

void test_main(TestConnections& test)
{
    // This needs to be set to prevent the SERVER_QUERY_WAS_SLOW status bit from showing up in OK packets.
    test.repl->execute_query_all_nodes("SET GLOBAL log_slow_query_time=3600");

    auto r = test.repl->backend(0)->admin_connection();
    auto table = r->create_table("test.t1", "id INT, val INT");
    auto user = r->create_user("mxs5268", "%", "mxs5268");
    user.grant("SELECT, UPDATE ON *.*");

    std::string sql = "INSERT INTO test.t1 VALUES (0,0)";

    for (int i = 1; i < NUM_THREADS; i++)
    {
        sql += ",(" + std::to_string(i) + ",0)";
    }

    r->cmd(sql);
    test.repl->sync_slaves();

    std::vector<std::thread> threads;

    for (int i = 0; i < NUM_THREADS; i++)
    {
        threads.emplace_back(query_thread, std::ref(test), i);
    }

    std::string previous = "server1";

    for (int i = 0; i < NUM_SWITCHOVERS && test.ok(); i++)
    {
        for (int s = 1; s <= 4 && test.ok(); s++)
        {
            std::string current = "server" + std::to_string(s);

            if (previous != current)
            {
                print_status(test, current);
                auto cmd = "--timeout=60s call command mariadbmon switchover MariaDB-Monitor " + current;
                test.check_maxctrl(cmd);
            }

            previous = current;
        }
        wait_for_progress(test, get_counters(threads.size()));
    }

    running = false;
    std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));

    test.repl->execute_query_all_nodes("SET GLOBAL log_slow_query_time=DEFAULT");
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
