/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

/**
 * MXS-1720: Test for causal_reads=fast
 *
 * https://jira.mariadb.org/browse/MXS-1720
 */

#include <maxtest/testconnections.hh>

void basic_test(TestConnections& test)
{
    test.maxscale->connect();
    test.try_query(test.maxscale->conn_rwsplit, "CREATE OR REPLACE TABLE test.t1(id INT)");

    for (int i = 0; i < 100; i++)
    {
        std::string value = std::to_string(i);
        std::string insert = "INSERT INTO test.t1 VALUES (" + value + ")";
        std::string select = "SELECT @@server_id, COUNT(*) FROM test.t1 WHERE id = " + value;

        test.try_query(test.maxscale->conn_rwsplit, "%s", insert.c_str());
        Row row = get_row(test.maxscale->conn_rwsplit, select);
        test.expect(!row.empty() && row[1] == "1", "At %d: Row is %s", i,
                    row.empty() ? "empty" : (row[0] + " " + row[1]).c_str());
    }

    test.try_query(test.maxscale->conn_rwsplit, "DROP TABLE test.t1");
    test.maxscale->disconnect();
}

void global_test(TestConnections& test)
{
    auto writer = test.maxscale->rwsplit();
    auto reader = test.maxscale->rwsplit();
    test.expect(writer.connect(), "Failed to connect writer: %s", writer.error());
    test.expect(reader.connect(), "Failed to connect reader: %s", reader.error());

    test.expect(writer.query("CREATE OR REPLACE TABLE test.t1(id INT)"),
                "Failed to create table: %s", writer.error());

    for (int i = 0; i < 100; i++)
    {
        std::string value = std::to_string(i);
        std::string insert = "INSERT INTO test.t1 VALUES (" + value + ")";
        std::string select = "SELECT @@server_id, COUNT(*) FROM test.t1 WHERE id = " + value;

        test.expect(writer.query(insert), "INSERT failed: %s", writer.error());

        auto row = reader.row(select);
        test.expect(!row.empty() && row[1] == "1", "At %d: Row is %s", i,
                    row.empty() ? "empty" : (row[0] + " " + row[1]).c_str());
    }

    writer.query("DROP TABLE test.t1");
}

int main(int argc, char** argv)
{
    TestConnections::require_repl_version("10.3.8");
    TestConnections test(argc, argv);

    basic_test(test);

    // MXS-4122: Fast global causal reads
    test.check_maxctrl("alter service RW-Split-Router causal_reads=fast_global");
    global_test(test);

    return test.global_result;
}
