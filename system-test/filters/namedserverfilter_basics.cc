/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/format.hh>
#include <maxtest/mariadb_connector.hh>
#include <maxtest/testconnections.hh>

using std::string;
using IdSet = std::set<int64_t>;

void test_query_target(TestConnections& test, mxt::MariaDB* conn, const IdSet& allowed_ids,
                       const string& query_part);
void exec_get_res(TestConnections& test, MYSQL_STMT* stmt, long expected, bool direct);

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

    auto srv_info = mxs.get_servers();
    srv_info.check_servers_status(mxt::ServersInfo::default_repl_states());
    const string twot = "twotargets";

    if (test.ok())
    {
        auto maxconn = mxs.open_rwsplit_connection2_nodb();
        // With all servers on, the query should go to either 2 or 3. Test several times.
        IdSet allowed = {srv_info.get(1).server_id, srv_info.get(2).server_id};
        for (int i = 0; i < 4 && test.ok(); i++)
        {
            test_query_target(test, maxconn.get(), allowed, twot);
        }
    }

    if (test.ok())
    {
        auto test_with_server_down = [&](int node_to_stop, int expected_node, const string& query_part) {
                repl.stop_node(node_to_stop);
                mxs.wait_for_monitor(1);
                auto& srv_stopped = srv_info.get(node_to_stop);
                auto& srv_expected = srv_info.get(expected_node);

                test.tprintf("Stopped  %s.", srv_stopped.name.c_str());
                test.tprintf("Query should go to %s.", srv_expected.name.c_str());
                IdSet allowed_set = {srv_expected.server_id};
                auto maxconn = mxs.open_rwsplit_connection2_nodb();

                for (int i = 0; i < 3 && test.ok(); i++)
                {
                    test_query_target(test, maxconn.get(), allowed_set, query_part);
                }

                repl.start_node(node_to_stop);
                test.tprintf("Restarted %s.", srv_stopped.name.c_str());
            };

        if (test.ok())
        {
            test_with_server_down(1, 2, twot);
            test_with_server_down(2, 1, twot);
        }

        if (test.ok())
        {
            test.check_maxctrl("alter filter NamedFilter target01 server1");
            test_with_server_down(3, 0, twot);

            mxs.wait_for_monitor(2);    // So monitor detects server4 start.
            test.check_maxctrl("alter filter NamedFilter target01 server2,server3");
        }

        if (test.ok())
        {
            auto test_with_all = [&](const std::set<int>& expected_nodes, const string& query_part) {
                    IdSet allowed_ids;
                    for (auto& node : expected_nodes)
                    {
                        auto& srv_expected = srv_info.get(node);
                        allowed_ids.insert(srv_expected.server_id);
                    }
                    auto maxconn = mxs.open_rwsplit_connection2_nodb();

                    for (int i = 0; i < 2; i++)
                    {
                        test_query_target(test, maxconn.get(), allowed_ids, query_part);
                    }
                };

            test_with_all({1}, "second server");
            test_with_all({2}, "third server");
            test_with_all({3}, "fourth server");
            test_with_all({0}, "master server");

            // The following two do not really test routing change, as the query goes to slave anyway.
            test_with_all({1, 2, 3}, "slave server");
            test_with_all({0, 1, 2, 3}, "all servers");

            if (test.ok())
            {
                test.tprintf("Test prepared statements.");
                mxs.connect_rwsplit("");
                auto* conn = mxs.conn_rwsplit;

                auto* stmt_s2 = mysql_stmt_init(conn);
                string query_s2 = "SELECT @@server_id, 'second server';";
                int rc_s2 = mysql_stmt_prepare(stmt_s2, query_s2.c_str(), query_s2.length());

                auto* stmt_s3 = mysql_stmt_init(conn);
                string query_s3 = "SELECT @@server_id, 'third server';";
                int rc_s3 = mysql_stmt_prepare(stmt_s3, query_s3.c_str(), query_s3.length());

                auto* stmt_s4 = mysql_stmt_init(conn);
                string query_s4 = "SELECT @@server_id, 'fourth server';";
                int rc_s4 = mysql_stmt_prepare(stmt_s4, query_s4.c_str(), query_s4.length());

                test.expect(rc_s2 == 0 && rc_s3 == 0 && rc_s4 == 0, "PS preparation failed.");
                if (test.ok())
                {
                    test.tprintf("Checking result of '%s'", query_s2.c_str());
                    exec_get_res(test, stmt_s2, srv_info.get(1).server_id, false);

                    test.tprintf("Checking result of '%s'", query_s3.c_str());
                    exec_get_res(test, stmt_s3, srv_info.get(2).server_id, false);

                    test.tprintf("Checking result of '%s'", query_s4.c_str());
                    exec_get_res(test, stmt_s4, srv_info.get(3).server_id, false);
                }

                mysql_stmt_close(stmt_s2);
                mysql_stmt_close(stmt_s3);
                mysql_stmt_close(stmt_s4);

                if (test.ok())
                {
                    test.tprintf("Checking result of '%s' when executed directly.", query_s4.c_str());
                    stmt_s4 = mysql_stmt_init(conn);
                    if (mariadb_stmt_execute_direct(stmt_s4, query_s4.c_str(), query_s4.length()) == 0)
                    {
                        exec_get_res(test, stmt_s4, srv_info.get(3).server_id, true);
                    }
                    else
                    {
                        test.add_failure("Direct exec failed. Error: %s", mysql_stmt_error(stmt_s4));
                    }
                    mysql_stmt_close(stmt_s4);
                }
            }
        }
    }

    if (test.ok())
    {
        /**
         * MXS-2037 Check that listener with "source"-parameter containing wildcard characters works.
         * This test only tests that ip addresses with wildcards are accepted by
         * NamedServerFilter. The actual matching functionality is not tested
         * because the client IPs can change with the different test environments
         * and that would make it complicated to check if the matching is correct.
         */
        test.tprintf("Test second listener.");
        auto conn = mxs.try_open_connection(4007, "maxuser", "maxuser");
        auto res = conn->simple_query("select 1;");
        if (conn->is_open() && res == "1")
        {
            test.tprintf("Second listener works");
        }
        else
        {
            test.add_failure("Connection/query to second listener failed.");
        }
    }
}

void test_query_target(TestConnections& test, mxt::MariaDB* conn, const IdSet& allowed_ids,
                       const string& query_part)
{
    const string q = mxb::string_printf("SELECT @@server_id, '%s';", query_part.c_str());
    auto res = conn->query(q);
    if (res && res->get_col_count() > 0 && res->next_row())
    {
        auto found_id = res->get_int(0);
        if (allowed_ids.count(found_id) > 0)
        {
            test.tprintf("Query '%s' went to server with id %li, as it should.", q.c_str(), found_id);
        }
        else
        {
            std::vector<string> allowed;
            for (auto& id : allowed_ids)
            {
                allowed.emplace_back(mxb::string_printf("%li", id));
            }
            string all_allowed = mxb::create_list_string(allowed, ", ", " or ");
            test.add_failure("Query '%s' went to server with id %li when %s was expected.",
                             q.c_str(), found_id, all_allowed.c_str());
        }
    }
    else
    {
        test.add_failure("Query '%s' failed or returned invalid data.", q.c_str());
    }
}

void exec_get_res(TestConnections& test, MYSQL_STMT* stmt, long expected, bool direct)
{
    if (direct || mysql_stmt_execute(stmt) == 0)
    {
        MYSQL_BIND results[2] {};
        long server_id = -1;
        char dummy[100] {};
        auto& res0 = results[0];
        res0.buffer_type = MYSQL_TYPE_LONGLONG;
        res0.buffer = &server_id;
        auto& res1 = results[1];
        res1.buffer_type = MYSQL_TYPE_STRING;
        res1.buffer = dummy;
        res1.buffer_length = sizeof(dummy);

        if (mysql_stmt_bind_result(stmt, results) == 0
            && mysql_stmt_store_result(stmt) == 0
            && mysql_stmt_fetch(stmt) == 0)
        {
            if (server_id == expected)
            {
                test.tprintf("Query returned %li, as it should.", server_id);
            }
            else
            {
                test.add_failure("Query returned %li, when %li was expected.",
                                 server_id, expected);
            }
        }
        else
        {
            test.add_failure("PS result bind/store/fetch failed. Error: %s",
                             mysql_stmt_error(stmt));
        }
    }
    else
    {
        test.add_failure("PS exec failed. Error: %s", mysql_stmt_error(stmt));
    }
}
