/**
 * MXS-3499: Prepared statement support for causal_reads
 *
 * https://jira.mariadb.org/browse/MXS-3499
 */

#include <maxtest/testconnections.hh>

void test_one_stmt(TestConnections& test, Connection& conn, MYSQL_STMT* stmt, int i)
{
    std::array<MYSQL_BIND, 2> param {};
    std::array<int, 2> value {};
    std::array<my_bool, 2> isnull {};

    param[0].buffer = &value[0];
    param[0].buffer_type = MYSQL_TYPE_LONG;
    param[0].is_null = &isnull[0];

    param[1].buffer = &value[1];
    param[1].buffer_type = MYSQL_TYPE_LONG;
    param[1].is_null = &isnull[1];

    test.set_timeout(30);
    test.expect(conn.query("INSERT INTO test.t1 VALUES (" + std::to_string(i) + ", repeat('a', 10000))"),
                "Failed to insert: %s", conn.error());

    test.set_timeout(30);
    test.expect(mysql_stmt_execute(stmt) == 0, "Execute failed: %s", mysql_stmt_error(stmt));
    mysql_stmt_bind_result(stmt, param.data());

    test.expect(mysql_stmt_fetch(stmt) == 0, "Fetch did not return enough rows");

    test.expect(value[1] == i, "Expected %d, got %d from server with ID %d", i, value[1], value[0]);

    test.expect(mysql_stmt_fetch(stmt) == MYSQL_NO_DATA, "Fetch returned too many rows");
    test.expect(!mysql_stmt_more_results(stmt), "Got more than one result");
    test.stop_timeout();
}

void run_test(TestConnections& test)
{
    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection should work");
    conn.query("CREATE OR REPLACE TABLE test.t1(id INT, data TEXT)");

    test.tprintf("Prepare a statement");

    MYSQL_STMT* stmt = conn.stmt();
    std::string query = "SELECT @@server_id, MAX(id) FROM test.t1";
    test.expect(mysql_stmt_prepare(stmt, query.c_str(), query.size()) == 0,
                "Prepare failed: %s", mysql_stmt_error(stmt));

    test.tprintf("Insert data and then execute the prepared statement");

    for (int i = 0; i < 100 && test.ok(); i++)
    {
        test_one_stmt(test, conn, stmt, i);
    }

    conn.query("TRUNCATE TABLE test.t1");

    test.tprintf("Set up a replication delay to force query retrying on the master");
    auto slave = test.repl->get_connection(1);
    slave.connect();
    slave.query("STOP SLAVE; CHANGE MASTER TO MASTER_DELAY=30; START SLAVE;");

    test.tprintf("Check that the queries are retried on the master if they fail on the slave");

    for (int i = 1; i <= 3; i++)
    {
        test_one_stmt(test, conn, stmt, i);
    }

    test.tprintf("Cleanup");

    slave.query("STOP SLAVE; CHANGE MASTER TO MASTER_DELAY=0; START SLAVE;");
    conn.query("DROP TABLE test.t1");
    mysql_stmt_close(stmt);
}

int main(int argc, char** argv)
{
    TestConnections::require_repl_version("10.3.8");
    TestConnections test(argc, argv);
    test.repl->execute_query_all_nodes("SET GLOBAL session_track_system_variables='last_gtid'");

    test.tprintf("Testing causal_reads=local");
    run_test(test);

    test.tprintf("Testing causal_reads=global");
    test.check_maxctrl("alter service RW-Split-Router causal_reads global");
    run_test(test);

    test.tprintf("Testing causal_reads=fast");
    test.check_maxctrl("alter service RW-Split-Router causal_reads fast");
    run_test(test);

    return test.global_result;
}
