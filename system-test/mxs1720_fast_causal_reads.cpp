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

int main(int argc, char** argv)
{
    TestConnections::require_repl_version("10.3.8");
    TestConnections test(argc, argv);

    test.repl->execute_query_all_nodes("SET GLOBAL session_track_system_variables='last_gtid'");

    basic_test(test);

    return test.global_result;
}
