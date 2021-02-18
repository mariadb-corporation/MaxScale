/**
 * MXS-1778: Use GTID from OK packets for consistent reads
 *
 * https://jira.mariadb.org/browse/MXS-1778
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    MariaDBCluster::require_gtid(true);
    TestConnections::require_repl_version("10.3.8");
    TestConnections test(argc, argv);
    test.repl->execute_query_all_nodes("SET GLOBAL session_track_system_variables='last_gtid'");

    auto conn = test.maxscales->rwsplit();
    conn.connect();
    test.expect(conn.query("CREATE OR REPLACE TABLE test.t1 (a LONGTEXT)"),
                "Table creation should work: %s", conn.error());
    conn.disconnect();

    std::string data(1000000, 'a');
    auto secondary = test.maxscales->rwsplit();
    secondary.connect();

    for (int i = 0; i < 50 && test.ok(); i++)
    {
        test.set_timeout(60);
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

    test.stop_timeout();

    conn.connect();
    test.expect(conn.query("DROP TABLE test.t1"),
                "Table creation should work: %s", conn.error());
    conn.disconnect();

    return test.global_result;
}
