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

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
