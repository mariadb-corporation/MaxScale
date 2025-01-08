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

void connection_hangs(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());
    auto master_id = c.field("SELECT @@server_id, @@last_insert_id");

    for (int i = 0; i < 10; i++)
    {
        std::string slow_query = "SET @a=(SELECT SLEEP(CASE @@server_id WHEN " + master_id
            + " THEN 0 ELSE 1 END))";
        test.expect(c.query(slow_query), "Failed to execute SET: %s", c.error());

        for (int j = 0; j < 10; j++)
        {
            MYSQL_STMT* stmt = c.stmt();
            std::string ps_query = "SELECT 1";
            test.expect(mysql_stmt_prepare(stmt, ps_query.c_str(), ps_query.length()) == 0,
                        "Prepare of '%s' failed: %s", ps_query.c_str(), mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
        }

        test.expect(c.query("SELECT @@server_id"), "Failed to execute SELECT: %s", c.error());
    }
}

void warning_on_sescmd_ps(TestConnections& test)
{
    test.maxctrl("stop monitor Monitor");
    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());
    test.repl->block_node(1);

    for (int i = 0; i < 5 && test.ok(); i++)
    {
        MYSQL_STMT* stmt = c.stmt();
        std::string ps_query = "SET NAMES 'utf8mb4' COLLATE 'utf8mb4_unicode_ci' ";
        test.expect(mysql_stmt_prepare(stmt, ps_query.c_str(), ps_query.length()) == 0,
                    "Prepare of '%s' failed: %s", ps_query.c_str(), mysql_stmt_error(stmt));
        test.expect(mysql_stmt_execute(stmt) == 0,
                    "Execute of '%s' failed: %s", ps_query.c_str(), mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
    }

    test.repl->unblock_node(1);

    test.expect(c.query("SELECT @@server_id"), "Failed to execute SELECT: %s", c.error());
    test.maxctrl("start monitor Monitor");
}

void backend_gets_closed(TestConnections& test)
{
    test.maxctrl("stop monitor Monitor");
    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());
    c.query("CREATE OR REPLACE TABLE test.t1(id INT)");
    c.disconnect();

    test.repl->suspend_node(0);
    c.connect();

    for (int i = 0; i < 100; i++)
    {
        c.send_query("INSERT INTO test.t1 VALUES (" + std::to_string(i) + ")");
    }

    test.repl->unsuspend_node(0);

    for (int i = 0; i < 100 && test.ok(); i++)
    {
        test.expect(c.read_query_result(), "Query %d failed: %s", i + 1, c.error());
    }

    test.maxctrl("start monitor Monitor");
}

void test_main(TestConnections& test)
{
    test.tprintf("MXS-5432: Connection hangs due to PS pruning");
    connection_hangs(test);

    test.tprintf("MXS-5443: Unknown prepared statement due to PS pruning");
    warning_on_sescmd_ps(test);

    test.tprintf("MXS-5443: Server with expected response gets closed due to packet limit being exceeded");
    backend_gets_closed(test);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
