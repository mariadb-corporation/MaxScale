/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

/**
 * MXS-1804: request 16M-1 stmt_prepare command packet connect hang
 *
 * https://jira.mariadb.org/browse/MXS-1804
 */

#include <maxtest/testconnections.hh>

std::string gen_sql(const std::string& prefix, unsigned int size, const std::string& suffix)
{
    std::string sql = prefix;
    sql.append(size - prefix.size() - suffix.size(), 'f');
    sql.append(suffix);
    return sql;
}

void mxs1804_long_ps_hang(TestConnections& test)
{
    for (int i = 0; i < 10; i += 3)
    {
        int sqlsize = 16777215 + i;

        std::string sql = gen_sql("select '", sqlsize, "'");

        test.reset_timeout();
        auto c = test.maxscale->rwsplit();
        c.connect();

        MYSQL_STMT* stmt = c.stmt();
        test.expect(mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) == 0,
                    "Prepare should not fail. Error: %s",
                    mysql_stmt_error(stmt));
        test.expect(mysql_stmt_execute(stmt) == 0, "Execute should not fail: %s",
                    mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);

        sql = gen_sql("INSERT INTO test.t1 VALUES ('", sqlsize, "')");

        stmt = c.stmt();
        test.expect(mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) == 0,
                    "Prepare for INSERT should not fail. Error: %s",
                    mysql_stmt_error(stmt));
        test.expect(mysql_stmt_execute(stmt) == 0, "Execute for INSERT should not fail: %s",
                    mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
    }
}

void mxs5556_hang_on_large_first_packet(TestConnections& test)
{
    std::string sql = gen_sql("select '", 16999999, "'");

    for (int i = 0; i < 25; i++)
    {
        auto c = test.maxscale->rwsplit();
        c.connect();

        MYSQL_STMT* stmt = c.stmt();
        test.expect(mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) == 0,
                    "Prepare should not fail. Error: %s",
                    mysql_stmt_error(stmt));
        test.expect(mysql_stmt_execute(stmt) == 0, "Execute should not fail: %s",
                    mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);

        c.query("SELECT 1");
        c.disconnect();
    }
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.repl->execute_query_all_nodes("SET GLOBAL max_allowed_packet=67108860");

    test.maxscale->connect_rwsplit();
    test.try_query(test.maxscale->conn_rwsplit, "CREATE OR REPLACE TABLE test.t1(data LONGTEXT)");
    test.repl->sync_slaves();

    mxs1804_long_ps_hang(test);
    mxs5556_hang_on_large_first_packet(test);

    test.maxscale->connect_rwsplit();
    test.try_query(test.maxscale->conn_rwsplit, "DROP TABLE test.t1");
    test.repl->sync_slaves();

    return test.global_result;
}
