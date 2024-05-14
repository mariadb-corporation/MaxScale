/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

void test_main(TestConnections& test)
{
    // Since we're using lazy_connect to delay the opening of the connections, make one query to force a slave
    // connection to be opened.
    auto c = test.maxscale->rwsplit();
    MXT_EXPECT(c.connect());
    MXT_EXPECT(c.query("SELECT 1"));

    // Keep one statement open for the duration of the test
    std::string sql_only_prepare = "SET @c = 3";
    MYSQL_STMT* prepare_only = c.stmt();

    // Repeatedly execute queries that are added to the history with both the text and the binary protocol.
    for (int i = 0; i < 5; i++)
    {
        MXT_EXPECT_F(c.query("SET @dummy = " + std::to_string(i)), "Query failed: %s", c.error());

        for (std::string query : {"SET @a = 1", "SET @b = 2"})
        {
            MYSQL_STMT* stmt = c.stmt();
            MXT_EXPECT_F(mysql_stmt_prepare(stmt, query.c_str(), query.length()) == 0,
                         "Prepare of '%s' failed: %s", query.c_str(), mysql_stmt_error(stmt));
            MXT_EXPECT_F(mysql_stmt_execute(stmt) == 0,
                         "Execute of '%s' failed: %s", query.c_str(), mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
        }

        MXT_EXPECT(mysql_stmt_prepare(prepare_only, sql_only_prepare.c_str(),
                                      sql_only_prepare.length()) == 0);
    }

    // This will be routed to the master because of LAST_INSERT_ID()
    auto res = c.row("SELECT @a, @b, @c, LAST_INSERT_ID()");
    MXT_EXPECT(res.size() == 4);
    MXT_EXPECT_F(res[0] == "1", "Expected @a to be 1 but got: '%s'", res[0].c_str());
    MXT_EXPECT_F(res[1] == "2", "Expected @b to be 2 but got: '%s'", res[1].c_str());
    MXT_EXPECT_F(res[2] == "NULL", "Expected @c to be NULL but got: '%s'", res[2].c_str());
    mysql_stmt_close(prepare_only);
}

int main(int argc, char** argv)
{
    try
    {
        return TestConnections().run_test(argc, argv, test_main);
    }
    catch (const std::exception& e)
    {
        return 1;
    }
}
