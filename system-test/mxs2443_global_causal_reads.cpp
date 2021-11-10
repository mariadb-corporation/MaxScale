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
    test.repl->execute_query_all_nodes("SET GLOBAL session_track_system_variables='last_gtid'");

    auto conn = test.maxscale->rwsplit();
    conn.connect();
    test.expect(conn.query("CREATE OR REPLACE TABLE test.t1 (a LONGTEXT)"),
                "Table creation should work: %s", conn.error());
    conn.disconnect();

    std::string data(1000000, 'a');
    auto secondary = test.maxscale->rwsplit();
    secondary.connect();

    for (int i = 0; i < 50 && test.ok(); i++)
    {
        test.reset_timeout();
        conn.connect();
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

    conn.connect();
    test.expect(conn.query("DROP TABLE test.t1"),
                "Table creation should work: %s", conn.error());
    conn.disconnect();

    // MXS-3856: Errors with causal_reads and read-only transactions
    readonly_trx_test(test);

    return test.global_result;
}
