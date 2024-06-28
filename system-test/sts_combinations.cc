/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Test arbitrary configuration generation with multi-layered services
 */

#include <maxtest/testconnections.hh>
#include <maxbase/json.hh>
#include <maxbase/string.hh>
#include <maxbase/stopwatch.hh>
#include <random>

using namespace std::chrono_literals;

static std::atomic<bool> running {true};
static std::minstd_rand0 seq(123456U);

class StsTester
{
public:
    using ConfigGenerator = std::function<std::string ()>;

    StsTester(TestConnections& test)
        : m_test(test)
    {
        // Create the root service and a listener for it
        auto service = next_service();
        cmd("create service " + service + " readwritesplit " + credentials());
        cmd("alter service " + service + " targets=server1,server2,server3,server4");
        cmd("create listener " + service + " listener0 4006");

        m_rels[service] = {"server1", "server2", "server3", "server4"};

        auto c = m_test.repl->get_connection(0);
        c.connect();
        c.query("CREATE OR REPLACE TABLE test.t1(id INT)");
        c.query("INSERT INTO test.t1 VALUES (1)");
    }

    ~StsTester()
    {
        auto c = m_test.repl->get_connection(0);
        c.connect();
        c.query("DROP TABLE test.t1");
    }

    void add_service()
    {
        auto victim = m_services[seq() % m_services.size()];
        create_service(victim, m_rels[victim]);
    }

    void remove_service()
    {
        auto victim = m_services[seq() % m_services.size()];

        // Don't destroy the root service
        if (victim != m_services.front())
        {
            destroy_service(find_parent(victim), victim, m_rels[victim]);
        }
    }

    void link_service()
    {
        auto svc = m_services[seq() % m_services.size()];
        auto target = m_services[seq() % m_services.size()];

        if (svc != target)
        {
            // Linking might fail if it would create a circular configuration
            if (try_cmd("link service " + svc + " " + target))
            {
                m_rels[svc].insert(target);
                m_test.tprintf("Link service '%s' to '%s'", svc.c_str(), target.c_str());
            }
        }
    }

private:
    void cmd(const std::string& arg)
    {
        m_test.check_maxctrl("--timeout=30s " + arg);
    }

    bool try_cmd(const std::string& arg)
    {
        return m_test.maxctrl(arg).rc == 0;
    }

    std::string next_service()
    {
        std::string svc = "service" + std::to_string(m_next_service_id++);
        m_services.push_back(svc);
        return svc;
    }

    std::string next_filter()
    {
        return "filter" + std::to_string(m_next_filter_id++);
    }
    std::string credentials() const
    {
        return "user=" + m_test.maxscale->user_name() + " password=" + m_test.maxscale->password();
    }

    std::string find_parent(const std::string& target)
    {
        for (const auto& parent_child : m_rels)
        {
            if (parent_child.second.find(target) != parent_child.second.end())
            {
                return parent_child.first;
            }
        }

        m_test.add_failure("Could not find parent for target '%s'", target.c_str());
        return "";
    }

    void create_service(const std::string& parent, std::set<std::string> children)
    {
        // Move a random number of servers from the parent service to the newly created service.
        int replace_count = std::max(1UL, seq() % children.size());
        auto it = std::next(children.begin(), replace_count % children.size());
        std::set<std::string> new_children(it, children.end());
        children.erase(it, children.end());

        auto new_service = next_service();
        auto router = random_router(new_children);
        cmd("create service " + new_service + " " + router + " " + credentials());

        std::uniform_int_distribution<> dist(0, 5);
        std::vector<std::string> filters;

        for (size_t n = dist(seq); n; n--)
        {
            auto new_filter = next_filter();
            auto filter = random_filter();
            cmd("create filter " + new_filter + " " + filter);
            m_test.tprintf("Create filter '%s': %s", new_filter.c_str(), filter.c_str());
            filters.push_back(new_filter);
            m_filters[new_service].insert(new_filter);
        }

        if (!filters.empty())
        {
            cmd("alter service-filters " + new_service + " " + mxb::join(filters, " "));
        }

        // Add the newly created service to the parent service.
        cmd("link service " + parent + " " + new_service);
        children.insert(new_service);

        cmd("alter service " + new_service + " targets=" + mxb::join(new_children));
        cmd("alter service " + parent + " targets=" + mxb::join(children));

        m_rels[new_service] = new_children;
        m_rels[parent] = children;

        m_test.tprintf("Create service '%s': %s", new_service.c_str(), router.c_str());
    }

    void destroy_service(const std::string& parent, const std::string& victim, std::set<std::string> children)
    {
        auto new_children = m_rels[parent];
        new_children.erase(victim);
        new_children.insert(children.begin(), children.end());

        cmd("unlink service " + parent + " " + victim);

        for (const auto& child : children)
        {
            cmd("link service " + parent + " " + child);
        }

        cmd("destroy service " + victim + " --force");
        m_rels[parent] = new_children;
        m_rels.erase(victim);
        m_services.erase(std::remove(m_services.begin(), m_services.end(), victim), m_services.end());

        for (const auto& f : m_filters[victim])
        {
            m_test.tprintf("Destroy filter '%s'", f.c_str());
            cmd("destroy filter " + f + " --force");
        }

        m_filters.erase(victim);

        std::string empty_service;

        for (auto& kv : m_rels)
        {
            kv.second.erase(victim);

            if (kv.second.empty())
            {
                empty_service = kv.first;
            }
        }

        m_test.tprintf("Destroy service '%s'", victim.c_str());

        if (!empty_service.empty())
        {
            m_test.tprintf("Recurse to '%s'", empty_service.c_str());
            auto grandfather = find_parent(empty_service);
            destroy_service(grandfather, empty_service, m_rels[grandfather]);
        }
    }

    ConfigGenerator constant(std::string str)
    {
        return [str](){
            return str;
        };
    }

    std::string random_router(const std::set<std::string>& new_children)
    {
        std::array routers {
            constant("readwritesplit"),
            constant("readwritesplit transaction_replay=true transaction_replay_timeout=5s"),
            constant("readwritesplit causal_reads=local"),

            constant("readconnroute router_options=running"),
            constant("readconnroute router_options=slave"),

            constant("schemarouter ignore_tables_regex=/.*/"),
            constant("schemarouter ignore_tables_regex=/.*/ "
                     "refresh_databases=true refresh_interval=10s max_staleness=5s"),
        };

        std::uniform_int_distribution<> dist(0, routers.size() - 1);
        return routers[dist(seq)]();
    }

    ConfigGenerator create_qlafilter()
    {
        return [this](){
            return "qlafilter log_type=unified filebase=/var/lib/maxscale/qlalog."
                   + std::to_string(m_next_file++) + ".txt";
        };
    }

    ConfigGenerator create_topfilter()
    {
        return [this](){
            return "topfilter filebase=/var/lib/maxscale/top-" + std::to_string(m_next_file++) + ".txt";
        };
    }

    std::string random_filter()
    {
        std::array filters {
            create_qlafilter(),
            constant("hintfilter"),
            constant("namedserverfilter match01=/SLEEP/ \"target01=->master\""),
            constant("regexfilter match=/SELECT/ replace=SELECT"),
            constant("ccrfilter count=5"),
            constant("ccrfilter time=5s"),
            constant("comment inject=hello"),
            constant("maxrows max_resultset_rows=1000000"),
            constant("optimistictrx"),
            constant("psreuse"),
            constant("throttlefilter max_qps=5000 throttling_duration=2s"),
            create_topfilter(),

            constant("cache storage=storage_inmemory cached_data=shared"),
            constant("cache storage=storage_inmemory"),
        };

        std::uniform_int_distribution<> dist(0, filters.size() - 1);
        return filters[dist(seq)]();
    }

    TestConnections&                             m_test;
    int                                          m_next_service_id {0};
    int                                          m_next_filter_id {0};
    int                                          m_next_file {0};
    std::vector<std::string>                     m_services;
    std::map<std::string, std::set<std::string>> m_filters;
    std::map<std::string, std::set<std::string>> m_rels;
};

void do_queries(TestConnections& test)
{
    while (running && test.ok())
    {
        std::default_random_engine query_rng(123456U);
        std::uniform_int_distribution dist(0, 10);
        auto c = test.maxscale->rwsplit();
        c.set_timeout(30);

        if (c.connect())
        {
            for (int i = 0; i < 5 && running && test.ok(); i++)
            {
                auto start = mxb::Clock::now();
                auto roll = dist(query_rng);

                if (roll > 25)
                {
                    c.query("SELECT 1 + SLEEP(RAND())");
                }
                else
                {
                    c.query("BEGIN");
                    c.query("SELECT 2 + SLEEP(RAND())");
                    c.query("SELECT 3 + SLEEP(RAND())");
                    c.query("SELECT 4 + SLEEP(RAND())");

                    if (roll < 5)
                    {
                        c.query("UPDATE test.t1 SET id = CONNECTION_ID()");
                    }

                    c.query("COMMIT");
                }

                auto end = mxb::Clock::now();
                test.expect(end - start < 15s, "[%u] Expected query to complete in under 15 seconds.",
                            c.thread_id());
            }
        }
        else
        {
            test.tprintf("Failed to connect: %s", c.error());
        }
    }
}

void test_main(TestConnections& test)
{
    StsTester tester(test);
    std::vector<std::thread> threads;
    std::uniform_int_distribution<> dist(0, 100);
    auto seed = std::random_device{}();
    test.tprintf("Random seed: 0x%x", seed);
    seq.seed(seed);

    for (int i = 0; i < 24; i++)
    {
        threads.emplace_back(do_queries, std::ref(test));
    }

    mxb::StopWatch sw;

    for (int i = 0; i < 1000 && sw.split() < 300s && test.ok(); i++)
    {
        test.reset_timeout();

        int dice_roll = dist(seq);

        if (dice_roll < 40)
        {
            tester.add_service();
        }
        else if (dice_roll < 80)
        {
            tester.link_service();
        }
        else
        {
            tester.remove_service();
        }
    }

    running = false;
    test.tprintf("Joining threads...");
    test.reset_timeout(30);

    for (auto& thr : threads)
    {
        thr.join();
    }

    // Clean up the old files
    test.maxscale->ssh_node_f(
        true, "find /var/lib/maxscale -name 'qlalog*.txt*' -o -name 'top*.txt*' -delete");

    // If the test failed, print the random seed again so that it's easy to find
    if (!test.ok())
    {
        test.tprintf("Random seed: 0x%x", seed);
    }
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
