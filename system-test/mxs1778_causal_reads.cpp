/**
 * MXS-1778: Use GTID from OK packets for consistent reads
 *
 * https://jira.mariadb.org/browse/MXS-1778
 */

#include <maxtest/testconnections.hh>

static std::string master;

void basic_test(TestConnections& test)
{
    test.tprintf("%s", __func__);
    const int N_QUERIES = 100;

    test.maxscale->connect();

    test.try_query(test.maxscale->conn_rwsplit, "CREATE OR REPLACE TABLE test.t1(id INT)");

    for (int i = 0; i < N_QUERIES; i++)
    {
        std::string value = std::to_string(i);
        std::string insert = "INSERT INTO test.t1 VALUES (" + value + ")";
        std::string select = "SELECT @@server_id, COUNT(*) FROM test.t1 WHERE id = " + value;

        test.try_query(test.maxscale->conn_rwsplit, "%s", insert.c_str());
        Row row = get_row(test.maxscale->conn_rwsplit, select);
        test.expect(!row.empty() && row [0] != master && row[1] == "1",
                    "At %d: Row is %s",
                    i,
                    row.empty() ? "empty" : (row[0] + " " + row[1]).c_str());
    }

    test.try_query(test.maxscale->conn_rwsplit, "DROP TABLE test.t1");

    test.maxscale->disconnect();
}

void master_retry_test(TestConnections& test)
{
    test.tprintf("%s", __func__);
    const int MAX_QUERIES = 10000;
    bool ok = false;

    test.maxctrl("alter service RW-Split-Router causal_reads_timeout 1s");

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection should work");
    conn.query("CREATE OR REPLACE TABLE test.t1(id INT)");


    for (int i = 0; i < MAX_QUERIES; i++)
    {
        conn.query("INSERT INTO test.t1 VALUES (" + std::to_string(i) + ")");
        conn.query("INSERT INTO test.t1 SELECT * FROM test.t1");
        auto row = conn.row("SELECT @@server_id");

        if (row[0] == master)
        {
            test.tprintf("Query number %d was retried on the master", i + 1);
            ok = true;
            break;
        }
    }

    conn.query("DROP TABLE test.t1");
    test.expect(ok, "Master should reply at least once");
}

int main(int argc, char** argv)
{
    TestConnections::require_repl_version("10.3.8");
    TestConnections test(argc, argv);

    test.repl->execute_query_all_nodes("SET GLOBAL session_track_system_variables='last_gtid'");
    test.repl->connect();
    master = get_row(test.repl->nodes[0], "SELECT @@server_id")[0];
    test.repl->disconnect();

    basic_test(test);
    master_retry_test(test);

    return test.global_result;
}
