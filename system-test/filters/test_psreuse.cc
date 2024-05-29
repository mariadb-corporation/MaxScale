/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxtest/maxrest.hh>

std::pair<int, int> get_stats(TestConnections& test)
{
    MaxRest api(&test);
    auto js = api.curl_get("filters/PsReuse");
    auto stats = js.at("data/attributes/filter_diagnostics");
    return {stats.get_int("hits"), stats.get_int("misses")};
}

void do_one(TestConnections& test, Connection& c, std::string sql)
{
    MYSQL_STMT* stmt = c.stmt();

    test.expect(mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) == 0,
                "Prepare failed: %s%s", mysql_stmt_error(stmt), c.error());

    test.expect(mysql_stmt_execute(stmt) == 0,
                "Execute failed: %s%s", mysql_stmt_error(stmt), c.error());

    mysql_stmt_close(stmt);
}

void do_one_direct(TestConnections& test, Connection& c, std::string sql)
{
    MYSQL_STMT* stmt = c.stmt();

    test.expect(mariadb_stmt_execute_direct(stmt, sql.c_str(), sql.size()) == 0,
                "Execute direct failed: %s%s", mysql_stmt_error(stmt), c.error());

    mysql_stmt_close(stmt);
}

void test_double_prepare(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    std::string sql = "SELECT 1";
    MXT_EXPECT(c.connect());

    MYSQL_STMT* stmt1 = c.stmt();
    MYSQL_STMT* stmt2 = c.stmt();

    test.expect(mysql_stmt_prepare(stmt1, sql.c_str(), sql.size()) == 0,
                "First prepare failed: %s%s", mysql_stmt_error(stmt1), c.error());

    test.expect(mysql_stmt_prepare(stmt2, sql.c_str(), sql.size()) != 0,
                "Second prepare succeeded");

    test.expect(mysql_stmt_execute(stmt1) == 0,
                "Execute failed: %s%s", mysql_stmt_error(stmt1), c.error());

    mysql_stmt_close(stmt1);
    mysql_stmt_close(stmt2);
}

void do_test(TestConnections& test, Connection c)
{
    MXT_EXPECT(c.connect());

    for (int i = 0; i < 5; i++)
    {
        do_one(test, c, "SELECT " + std::to_string(i));
    }

    auto [hits, misses] = get_stats(test);
    MXT_EXPECT_F(hits == 0, "Expected 0 hits, got %d", hits);
    MXT_EXPECT_F(misses == 5, "Expected 5 misses, got %d");

    for (int i = 0; i < 5; i++)
    {
        do_one(test, c, "SELECT " + std::to_string(i));
    }

    std::tie(hits, misses) = get_stats(test);
    MXT_EXPECT_F(hits == 5, "Expected 5 hits, got %d", hits);
    MXT_EXPECT_F(misses == 5, "Expected 5 misses, got %d", misses);

    for (int i = 0; i < 5; i++)
    {
        do_one_direct(test, c, "SELECT " + std::to_string(i));
    }

    std::tie(hits, misses) = get_stats(test);
    MXT_EXPECT_F(hits == 10, "Expected 10 hits, got %d", hits);
    MXT_EXPECT_F(misses == 5, "Expected 5 misses, got %d", misses);

    // If the SQL doesn't fit into a single network packet, it will not be cached.
    std::string big_constant = ", '";
    big_constant.append(1024 * 1024 * 17, 'a');
    big_constant += "'";

    for (int i = 0; i < 5; i++)
    {
        do_one(test, c, "SELECT " + std::to_string(i) + big_constant);
    }

    std::tie(hits, misses) = get_stats(test);
    MXT_EXPECT_F(hits == 10, "Expected 10 hits, got %d", hits);
    MXT_EXPECT_F(misses == 5, "Expected 5 misses, got %d", misses);

    for (int i = 0; i < 5; i++)
    {
        do_one_direct(test, c, "SELECT " + std::to_string(i) + big_constant);
    }

    std::tie(hits, misses) = get_stats(test);
    MXT_EXPECT_F(hits == 10, "Expected 10 hits, got %d", hits);
    MXT_EXPECT_F(misses == 5, "Expected 5 misses, got %d", misses);
}

void test_main(TestConnections& test)
{
    test.repl->connect();
    test.repl->execute_query_all_nodes("SET GLOBAL max_allowed_packet=1073741824");
    test.repl->disconnect();

    do_test(test, test.maxscale->rwsplit());

    test.maxscale->restart();
    test.maxscale->wait_for_monitor();

    do_test(test, test.maxscale->readconn_master());

    test_double_prepare(test);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
