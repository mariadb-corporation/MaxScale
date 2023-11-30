/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.repl->execute_query_all_nodes("STOP SLAVE; RESET SLAVE;");

    for (int i = 0; i < test.repl->N; i++)
    {
        auto repl = test.repl->get_connection(i);
        test.expect(repl.connect()
                    && repl.query("CREATE DATABASE db1")
                    && repl.query("CREATE TABLE db1.t" + std::to_string(i) + "(id INT)")
                    && repl.query("INSERT INTO db1.t" + std::to_string(i) + " VALUES (@@server_id)"),
                    "Failed to create table on node %d: %s", i, repl.error());
    }

    auto c = test.maxscale->rwsplit();
    c.connect();

    test.expect(c.query("USE db1"), "USE db1 failed: %s", c.error());

    for (int i = 0; i < test.repl->N; i++)
    {
        auto table = "t" + std::to_string(i);
        test.expect(c.query("SELECT id FROM " + table),
                    "SELECT from %s failed: %s", table.c_str(), c.error());
    }

    c.disconnect();
    c.connect();

    test.expect(c.change_db("db1"), "COM_INIT_DB to db1 failed: %s", c.error());

    for (int i = 0; i < test.repl->N; i++)
    {
        auto table = "t" + std::to_string(i);
        test.expect(c.query("SELECT id FROM " + table),
                    "SELECT from %s failed: %s", table.c_str(), c.error());
    }

    test.repl->execute_query_all_nodes("DROP DATABASE db1");

    return test.global_result;
}
