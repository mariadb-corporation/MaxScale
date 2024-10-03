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

/**
 * MXS-5302: Executing more than max_sescmd_history prepared statement loses some of them
 * https://jira.mariadb.org/browse/MXS-5302
 */

#include <maxtest/testconnections.hh>

const std::string VALUE = "foobar";
const std::string NO_VALUE = "";

void execute_statements(TestConnections& test, const std::vector<MYSQL_STMT*>& stmts)
{
    for (MYSQL_STMT* stmt : stmts)
    {
        if (!test.expect(mysql_stmt_execute(stmt) == 0,
                         "Failed to execute statement: %s", mysql_stmt_error(stmt)))
        {
            break;
        }

        mysql_stmt_free_result(stmt);
    }
}

void check_user_variables(TestConnections& test, Connection& c, int prune_limit)
{
    std::string var = "a";

    for (int i = 0; i < 15; i++)
    {
        auto res = c.field("SELECT @" + var);
        auto expected = i < prune_limit ? NO_VALUE : VALUE;
        test.expect(res == expected,
                    "Expected variable number %d (@%s) to be `%s`, but it was: `%s`",
                    i + 1, var.c_str(), expected.c_str(), res.c_str());
        var[0]++;
    }
}

void test_mxs5302(TestConnections& test)
{
    const std::string USER = "mxs2464_sescmd_reconnect";
    const std::string PASSWORD = "mxs2464_sescmd_reconnect";

    auto r = test.repl->get_connection(0);
    r.connect();
    r.query("CREATE USER " + USER + " IDENTIFIED BY '" + PASSWORD + "'");
    r.query("GRANT ALL ON *.* TO " + USER);
    test.repl->sync_slaves();

    auto c = test.maxscale->rwsplit();
    c.set_credentials(USER, PASSWORD);
    c.connect();
    std::vector<MYSQL_STMT*> stmts;
    std::string query = "SELECT 1";
    std::string var = "a";

    // First, set 5 user variables.
    for (int i = 0; i < 5; i++)
    {
        c.query("SET @" + var + "='" + VALUE + "'");
        var[0]++;
    }

    // Then, prepare 10 prepared statements and set 10 user variables.
    for (int i = 0; i < 10; i++)
    {
        MYSQL_STMT* stmt = c.stmt();

        if (!test.expect(mysql_stmt_prepare(stmt, query.c_str(), query.size()) == 0,
                         "Failed to prepare statement: %s", mysql_stmt_error(stmt)))
        {
            break;
        }

        stmts.push_back(stmt);

        c.query("SET @" + var + "='" + VALUE + "'");
        var[0]++;
    }

    // Execute some queries to make sure the backends have executed the session commands.
    for (int i = 0; i < 10; i++)
    {
        c.query("SELECT 1");
    }

    // All of the prepared statements and user variables should exist before the reconnection.
    execute_statements(test, stmts);
    check_user_variables(test, c, 0);

    test.log_printf("Killing all connections");
    test.repl->execute_query_all_nodes(("KILL USER " + USER).c_str());

    // After the reconnection, all of the prepared statements should exist but only the last 5 user variables
    // should exist.
    execute_statements(test, stmts);
    check_user_variables(test, c, 10);

    for (auto* stmt : stmts)
    {
        mysql_stmt_close(stmt);
    }

    r.query("DROP USER " + USER);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_mxs5302);
}
