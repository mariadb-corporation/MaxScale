/**
 * MXS-1245: Test batch execution of various queries
 */

#include <maxtest/testconnections.hh>

void send_batch(TestConnections& test, MYSQL* mysql, int num_query, const std::string& query)
{
    for (int i = 0; i < num_query && test.ok(); i++)
    {
        test.reset_timeout();
        test.expect(mysql_send_query(mysql, query.c_str(), query.length()) == 0,
                    "Batch query failed for '%s': %s", query.c_str(), mysql_error(mysql));
    }
}

void read_results(TestConnections& test, MYSQL* mysql, int num_query)
{
    for (int i = 0; i < num_query && test.ok(); i++)
    {
        test.reset_timeout();
        test.expect(mysql_read_query_result(mysql) == 0,
                    "Reading batch result failed: %s", mysql_error(mysql));
        mysql_free_result(mysql_use_result(mysql));
    }
}

void run_test(TestConnections& test, MYSQL* mysql, const std::string& query)
{
    const int NUM_QUERY = 50;
    send_batch(test, mysql, NUM_QUERY, query);
    read_results(test, mysql, NUM_QUERY);
}

void test_master_failure(TestConnections& test, MYSQL* mysql)
{
    const std::string query = "DO LAST_INSERT_ID(), SLEEP(5)";
    const int NUM_QUERY = 6;

    send_batch(test, mysql, NUM_QUERY, query);

    test.reset_timeout();
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor(2);
    test.repl->unblock_node(0);
    test.maxscale->wait_for_monitor(2);

    for (int i = 0; i < NUM_QUERY && test.ok(); i++)
    {
        test.reset_timeout();
        mysql_read_query_result(mysql);
    }
}

void test_trx_replay(TestConnections& test, MYSQL* mysql)
{
    // Enable transaction_replay and reconnect to take it into use
    test.check_maxctrl("alter service RW-Split-Router transaction_replay true");
    test.check_maxctrl("alter service RW-Split-Router delayed_retry_timeout 30s");
    test.maxscale->connect_rwsplit();
    mysql = test.maxscale->conn_rwsplit[0];

    const std::string query = "SELECT SLEEP(1)";
    const int NUM_QUERY = 15;

    test.expect(mysql_query(mysql, "BEGIN") == 0, "BEGIN should work: %s", mysql_error(mysql));
    send_batch(test, mysql, NUM_QUERY, query);

    // Give the server some time to execute the queries
    sleep(5);

    test.reset_timeout();
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor(2);
    test.repl->unblock_node(0);
    test.maxscale->wait_for_monitor(2);

    read_results(test, mysql, NUM_QUERY);

    test.expect(mysql_query(mysql, "COMMIT") == 0, "COMMIT should work: %s", mysql_error(mysql));

    // Revert the configuration change and reconnect
    test.check_maxctrl("alter service RW-Split-Router transaction_replay false");
    test.maxscale->connect_rwsplit();
}

void test_optimistic_trx(TestConnections& test, MYSQL* mysql)
{
    // Enable optimistic_trx and reconnect to take it into use
    test.check_maxctrl("alter service RW-Split-Router optimistic_trx true");
    test.maxscale->connect_rwsplit();
    mysql = test.maxscale->conn_rwsplit[0];

    const std::string read_query = "SELECT * FROM test.t1";
    const std::string write_query = "INSERT INTO test.t1 VALUES (1)";
    const int NUM_QUERY = 15;

    test.tprintf("  Test successful optimistic transaction execution");

    test.expect(mysql_query(mysql, "BEGIN") == 0, "BEGIN should work: %s", mysql_error(mysql));
    send_batch(test, mysql, NUM_QUERY, read_query);
    read_results(test, mysql, NUM_QUERY);
    test.expect(mysql_query(mysql, "COMMIT") == 0, "COMMIT should work: %s", mysql_error(mysql));

    test.tprintf("  Test optimistic transaction execution with writes in the middle of the transaction");


    test.expect(mysql_query(mysql, "BEGIN") == 0, "BEGIN should work: %s", mysql_error(mysql));
    send_batch(test, mysql, NUM_QUERY, read_query);
    send_batch(test, mysql, NUM_QUERY, write_query);
    read_results(test, mysql, NUM_QUERY * 2);
    test.expect(mysql_query(mysql, "COMMIT") == 0, "COMMIT should work: %s", mysql_error(mysql));

    // Revert the configuration change and reconnect
    test.check_maxctrl("alter service RW-Split-Router optimistic_trx false");
    test.maxscale->connect_rwsplit();
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
        "SELECT * FROM test.t1",
        "SELECT LAST_INSERT_ID()",
    };

    test.maxscale->connect_rwsplit();
    mysql_query(test.maxscale->conn_rwsplit[0], "CREATE TABLE test.t1(id INT)");

    test.log_printf("Testing streaming of various queries");

    for (const auto& query : queries)
    {
        test.tprintf("  %s", query.c_str());
        run_test(test, test.maxscale->conn_rwsplit[0], query);
    }

    test.log_printf("Run the same test but inside a transaction");

    for (const auto& query : queries)
    {
        test.tprintf("  %s", query.c_str());
        mysql_query(test.maxscale->conn_rwsplit[0], "START TRANSACTION");
        run_test(test, test.maxscale->conn_rwsplit[0], query);
        mysql_query(test.maxscale->conn_rwsplit[0], "COMMIT");
    }

    test.log_printf("Testing master failure during query streaming");
    test_master_failure(test, test.maxscale->conn_rwsplit[0]);

    test.log_printf("Testing transaction_replay with query streaming");
    test_trx_replay(test, test.maxscale->conn_rwsplit[0]);

    test.log_printf("Testing optimistic_trx with query streaming");
    test_optimistic_trx(test, test.maxscale->conn_rwsplit[0]);

    mysql_query(test.maxscale->conn_rwsplit[0], "DROP TABLE test.t1");

    return test.global_result;
}
