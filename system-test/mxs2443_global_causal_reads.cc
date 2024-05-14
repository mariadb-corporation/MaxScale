/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

/**
 * MXS-1778: Use GTID from OK packets for consistent reads
 *
 * https://jira.mariadb.org/browse/MXS-1778
 */

#include <maxtest/testconnections.hh>

void readonly_trx_test(TestConnections& test)
{
    // Create a table and insert some data into it
    auto first = test.maxscale->rwsplit();
    test.expect(first.connect(), "Connection should work");
    first.query("CREATE OR REPLACE TABLE test.t1(id INT)");
    first.query("INSERT INTO test.t1 VALUES (1)");

    // Open a second connection and start a read-only transaction
    auto second = test.maxscale->rwsplit();
    test.expect(second.connect(), "Connection should work");
    second.query("START TRANSACTION READ ONLY");

    // Stop the monitor and stop replication on all the slave servers
    test.maxctrl("stop monitor MySQL-Monitor");
    test.repl->execute_query_all_nodes("STOP SLAVE");

    // Insert a row to generate the next GTID
    first.query("INSERT INTO test.t1 VALUES (2)");

    bool ok = second.query("SELECT COUNT(*) FROM test.t1");
    std::string err = second.error();
    test.expect(!ok, "Causal read should fail");
    test.expect(err.find("Causal read timed out") != std::string::npos,
                "Wrong error message: %s", err.c_str());

    // Resume replication, query should now work
    test.repl->execute_query_all_nodes("START SLAVE");

    ok = second.query("SELECT COUNT(*) FROM test.t1");
    test.expect(ok, "Causal read should work: %s", second.error());
    second.query("COMMIT");

    // Cleanup
    first.query("DROP TABLE test.t1");
}

int main(int argc, char** argv)
{
    TestConnections::require_repl_version("10.3.8");
    TestConnections test(argc, argv);
    test.repl->set_replication_delay(1);

    auto conn = test.maxscale->rwsplit();
    conn.connect();
    test.expect(conn.query("CREATE OR REPLACE TABLE test.t1 (a INT)"),
                "Table creation should work: %s", conn.error());
    conn.disconnect();

    auto secondary = test.maxscale->rwsplit();
    secondary.connect();

    for (int i = 0; i < 20 && test.ok(); i++)
    {
        test.reset_timeout();
        conn.connect();
        std::string data = std::to_string(i);
        test.expect(conn.query("INSERT INTO test.t1 VALUES ('" + data + "')"),
                    "INSERT should work: %s", conn.error());

        // Existing connections should also see the inserted rows
        auto count = atoi(secondary.field("SELECT COUNT(*) FROM test.t1").c_str());
        test.expect(count == i + 1, "Missing `%d` rows from open connection.", (i + 1) - count);

        conn.disconnect();

        // New connections should see the inserted rows
        conn.connect();
        auto second_count = atoi(conn.field("SELECT COUNT(*) FROM test.t1").c_str());
        test.expect(second_count == i + 1, "Missing `%d` rows.", (i + 1) - second_count);
        conn.disconnect();
    }

    auto res = test.maxctrl("api get services/RW-Split-Router data.attributes.router_diagnostics.last_gtid");
    auto gtid_pos = res.output;

    conn.connect();
    test.expect(conn.query("DROP TABLE test.t1"),
                "Table creation should work: %s", conn.error());
    conn.disconnect();

    // MXS-3856: Errors with causal_reads and read-only transactions
    readonly_trx_test(test);

    test.repl->set_replication_delay(0);

    test.check_maxctrl("call command readwritesplit reset-gtid RW-Split-Router");
    res = test.maxctrl("api get services/RW-Split-Router data.attributes.router_diagnostics.last_gtid");
    test.expect(gtid_pos != res.output, "Global GTID state should be reset: %s != %s",
                gtid_pos.c_str(), res.output.c_str());
    test.expect(res.output == "null", "Global GTID state should be null: %s", res.output.c_str());

    return test.global_result;
}
