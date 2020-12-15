/**
 * MXS-1245: Test batch execution of various queries
 */

#include <maxtest/testconnections.hh>

void run_test(TestConnections& test, MYSQL* mysql, const std::string& query)
{
    const int NUM_QUERY = 50;

    for (int i = 0; i < NUM_QUERY && test.ok(); i++)
    {
        test.set_timeout(10);
        test.expect(mysql_send_query(mysql, query.c_str(), query.length()) == 0,
                    "Batch query failed for '%s': %s", query.c_str(), mysql_error(mysql));
    }

    for (int i = 0; i < NUM_QUERY && test.ok(); i++)
    {
        test.set_timeout(10);
        test.expect(mysql_read_query_result(mysql) == 0,
                    "Reading batch result failed: %s", mysql_error(mysql));
    }

    test.stop_timeout();
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    std::vector<std::string> queries =
    {
        "INSERT INTO test.t1 VALUES (1)",
        "UPDATE test.t1 SET id = id + 1 WHERE id MOD 2 != 0",
        "DELETE FROM test.t1 LIMIT 1",
        "SET @a = 1",   // Currently this won't be executed in a pipeline manner
        // "SELECT * FROM test.t1", // TODO: Enable this when the connector supports it
    };

    test.maxscales->connect_rwsplit();
    mysql_query(test.maxscales->conn_rwsplit[0], "CREATE TABLE test.t1(id INT)");

    for (const auto& query : queries)
    {
        test.tprintf("%s", query.c_str());

        run_test(test, test.maxscales->conn_rwsplit[0], query);

        // Run the same test but inside a transaction
        mysql_query(test.maxscales->conn_rwsplit[0], "START TRANSACTION");
        run_test(test, test.maxscales->conn_rwsplit[0], query);
        mysql_query(test.maxscales->conn_rwsplit[0], "COMMIT");
    }

    mysql_query(test.maxscales->conn_rwsplit[0], "DROP TABLE test.t1");

    return test.global_result;
}
