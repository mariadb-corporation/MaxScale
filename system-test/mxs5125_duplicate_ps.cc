/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-09-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

void duplicate_ps(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    MXT_EXPECT(c.connect());
    MXT_EXPECT(c.query("DROP USER IF EXISTS duplicate_ps"));
    MXT_EXPECT(c.query("CREATE USER duplicate_ps IDENTIFIED BY 'duplicate_ps'"));
    MXT_EXPECT(c.query("GRANT ALL ON *.* TO duplicate_ps"));
    c.set_credentials("duplicate_ps", "duplicate_ps");
    MXT_EXPECT(c.connect());

    auto stmts = {c.stmt(), c.stmt(), c.stmt(), c.stmt()};

    for (auto stmt : stmts)
    {
        std::string query = "SELECT 1";
        MXT_EXPECT_F(mysql_stmt_prepare(stmt, query.c_str(), query.size()) == 0,
                     "Prepare failed: %s%s", mysql_stmt_error(stmt), c.error());
    }

    for (auto stmt : stmts)
    {
        MXT_EXPECT_F(mysql_stmt_execute(stmt) == 0,
                     "Execute failed: %s%s", mysql_stmt_error(stmt), c.error());

        while (mysql_stmt_fetch(stmt) == 0)
        {
        }
    }

    test.repl->connect();
    test.repl->execute_query_all_nodes("KILL CONNECTION USER duplicate_ps");
    test.repl->disconnect();

    for (auto stmt : stmts)
    {
        MXT_EXPECT_F(mysql_stmt_execute(stmt) == 0,
                     "Execute failed: %s%s", mysql_stmt_error(stmt), c.error());

        while (mysql_stmt_fetch(stmt) == 0)
        {
        }
    }

    for (auto stmt : stmts)
    {
        mysql_stmt_close(stmt);
    }

    c = test.maxscale->rwsplit();
    c.connect();
    MXT_EXPECT(c.query("DROP USER IF EXISTS duplicate_ps"));
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, duplicate_ps);
}
