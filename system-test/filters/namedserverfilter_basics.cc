/**
 * @file namedserverfilter.cpp Namedserverfilter test
 *
 * Check that a readwritesplit service with a namedserverfilter will route a
 * SELECT @@server_id to the correct server. The filter is configured with
 * `match=SELECT` which should match any SELECT query.
 */

#include <maxtest/testconnections.hh>
#include <iostream>
#include <maxtest/mariadb_connector.hh>
#include <maxbase/format.hh>

using std::string;
using IdSet = std::set<int64_t>;

void check_server_id(TestConnections& test, mxt::MariaDB* conn, const IdSet& allowed_ids);
void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    const int iterations = 7;

    auto srv_info = mxs.get_servers();
    srv_info.check_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        auto maxconn = mxs.open_rwsplit_connection2_nodb();
        test.tprintf("Testing with all servers on. Select-queries should go to %s and %s.",
                     srv_info.get(1).name.c_str(),  srv_info.get(2).name.c_str());
        // With all servers on, the query should go to either 2 or 3. Test several times.

        IdSet allowed = {srv_info.get(1).server_id, srv_info.get(2).server_id};
        for (int i = 0; i < iterations && test.ok(); i++)
        {
            check_server_id(test, maxconn.get(), allowed);
        }
    }

    if (test.ok())
    {
        auto test_server_down = [&](int node_to_stop, int allowed_node) {
                repl.stop_node(node_to_stop);
                mxs.wait_for_monitor(1);
                auto& srv_stopped = srv_info.get(node_to_stop);
                auto& srv_expected = srv_info.get(allowed_node);

                test.tprintf("Stopped  %s.", srv_stopped.name.c_str());
                test.tprintf("Select-queries should go to %s.", srv_expected.name.c_str());
                IdSet allowed_set = {srv_expected.server_id};
                auto maxconn = mxs.open_rwsplit_connection2_nodb();

                // Test that queries only go to the correct server.
                for (int i = 0; i < iterations && test.ok(); i++)
                {
                    check_server_id(test, maxconn.get(), allowed_set);
                }

                repl.start_node(node_to_stop);
                test.tprintf("Restarted %s.", srv_stopped.name.c_str());
            };

        if (test.ok())
        {
            test_server_down(1, 2);
        }
        if (test.ok())
        {
            test_server_down(2, 1);
        }
        if (test.ok())
        {
            test.check_maxctrl("alter filter namedserverfilter target01 server1");
            test_server_down(3, 0);
        }

        // TODO: Test ->master, ->slave and ->all tags
    }
}

void check_server_id(TestConnections& test, mxt::MariaDB* conn, const IdSet& allowed_ids)
{
    const string q = "SELECT @@server_id;";
    auto res = conn->query(q);
    if (res && res->get_col_count() > 0 && res->next_row())
    {
        auto found_id = res->get_int(0);
        if (allowed_ids.count(found_id) == 0)
        {
            std::vector<string> allowed;
            for (auto& id : allowed_ids)
            {
                allowed.emplace_back(mxb::string_printf("%li", id));
            }
            string all_allowed = mxb::create_list_string(allowed, ", ", " or ");
            test.add_failure("Query '%s' returned %li when %s was expected.",
                             q.c_str(), found_id, all_allowed.c_str());
        }
    }
    else
    {
        test.add_failure("Query '%s' failed or returned invalid data.", q.c_str());
    }
}
