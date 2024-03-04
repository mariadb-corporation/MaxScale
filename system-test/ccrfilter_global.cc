/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Test global mode for the CCRFilter
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto conn = test.maxscale->rwsplit();
    conn.connect();
    test.expect(conn.query("CREATE OR REPLACE TABLE test.t1 (a LONGTEXT)"),
                "Table creation should work: %s", conn.error());
    conn.disconnect();

    std::string data(1000000, 'a');
    auto secondary = test.maxscale->rwsplit();
    secondary.connect();

    for (int i = 0; i < 25; i++)
    {
        conn.connect();
        test.expect(conn.query("INSERT INTO test.t1 VALUES ('" + data + "')"),
                    "INSERT should work: %s", conn.error());
        conn.disconnect();

        // New connections should see the inserted rows
        conn.connect();
        auto count = std::stoi(conn.field("SELECT COUNT(*) FROM test.t1"));
        test.expect(count == i + 1, "Missing `%d` rows.", (i + 1) - count);
        conn.disconnect();

        // Existing connections should also see the inserted rows
        auto second_count = std::stoi(secondary.field("SELECT COUNT(*) FROM test.t1"));
        test.expect(second_count == i + 1, "Missing `%d` rows from open connection.", (i + 1) - count);

        // Make sure the row is replicated before inserting another one
        test.repl->sync_slaves();
    }

    conn.connect();
    test.expect(conn.query("DROP TABLE test.t1"),
                "Table creation should work: %s", conn.error());
    conn.disconnect();

    return test.global_result;
}
