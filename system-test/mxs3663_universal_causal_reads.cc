/**
 * MXS-3663: Universal causal reads
 *
 * https://jira.mariadb.org/browse/MXS-3663
 */

#include <maxtest/testconnections.hh>
#include <mysqld_error.h>

std::atomic<bool> running {true};
std::atomic<int> id{1};

void test_reads(TestConnections& test)
{
    std::string table = "test.t" + std::to_string(id++);
    auto conn = test.maxscale->rwsplit();
    conn.connect();
    test.expect(conn.query("CREATE OR REPLACE TABLE " + table + " (a INT)"),
                "Table creation should work: %u, %s", conn.thread_id(), conn.error());
    conn.disconnect();

    auto secondary = test.maxscale->rwsplit();
    secondary.connect();

    for (int i = 0; i < 10 && running && test.ok(); i++)
    {
        test.reset_timeout();
        conn.connect();
        test.expect(conn.query("INSERT INTO " + table + " VALUES ('" + std::to_string(i) + "')"),
                    "INSERT should work: %u, %s", conn.thread_id(), conn.error());

        // Existing connections should also see the inserted rows
        auto count = atoi(secondary.field("SELECT COUNT(*) FROM " + table).c_str());
        test.expect(count == i + 1, "Missing %d rows from open connection.", (i + 1) - count);
        conn.disconnect();

        // New connections should see the inserted rows
        conn.connect();
        auto second_count = atoi(conn.field("SELECT COUNT(*) FROM " + table).c_str());
        test.expect(second_count == i + 1, "Missing %d rows.", (i + 1) - second_count);
        conn.disconnect();
    }
}

void test_queries(TestConnections& test, std::initializer_list<std::string> before,
                  std::initializer_list<std::string> after, bool ignore_errors = false)
{
    std::string table = "test.t" + std::to_string(id++);
    auto conn = test.maxscale->rwsplit();
    conn.connect();
    test.expect(conn.query("CREATE OR REPLACE TABLE " + table + " (a INT)"),
                "Table creation should work: %u, %s", conn.thread_id(), conn.error());
    conn.disconnect();

    for (int i = 0; i < 100 && running && test.ok(); i++)
    {
        test.reset_timeout();
        conn.connect();

        for (const auto& query : before)
        {
            test.expect(conn.query(query),
                        "%s should work: %u, %s", query.c_str(), conn.thread_id(), conn.error());
        }

        bool ok = conn.query("INSERT INTO " + table + " VALUES ('" + std::to_string(i) + "')");
        bool ro_error = conn.errnum() == ER_CANT_EXECUTE_IN_READ_ONLY_TRANSACTION && ignore_errors;
        test.expect(ok || ignore_errors, "INSERT should work: %u, %s", conn.thread_id(), conn.error());
        auto first_count = atoi(conn.field("SELECT COUNT(*) FROM " + table).c_str());
        test.expect(first_count == i + 1 || ro_error, "Missing %d rows.", (i + 1) - first_count);


        for (const auto& query : after)
        {
            test.expect(conn.query(query),
                        "%s should work: %u, %s", query.c_str(), conn.thread_id(), conn.error());
        }

        conn.disconnect();
        conn.connect();
        auto second_count = atoi(conn.field("SELECT COUNT(*) FROM " + table).c_str());
        test.expect(second_count == i + 1 || ro_error, "Missing %d rows.", (i + 1) - second_count);
        conn.disconnect();
    }
}

void test_no_trx(TestConnections& test)
{
    test_queries(test, {}, {});
}

void test_rw_trx(TestConnections& test)
{
    test_queries(test, {"START TRANSACTION"}, {"COMMIT"});
}

void test_autocommit_on(TestConnections& test)
{
    test_queries(test, {"SET autocommit=1"}, {});
}

void test_autocommit_off(TestConnections& test)
{
    test_queries(test, {"SET autocommit=0"}, {"COMMIT"});
}

void test_ro_trx(TestConnections& test)
{
    test_queries(test, {"START TRANSACTION READ ONLY"}, {"COMMIT"}, true);
}
void test_ro_trx_set_trx(TestConnections& test)
{
    test_queries(test, {"SET TRANSACTION READ ONLY", "START TRANSACTION"}, {"COMMIT"}, true);
}

int main(int argc, char** argv)
{
    TestConnections::require_repl_version("10.3.8");
    TestConnections test(argc, argv);
    test.repl->set_replication_delay(1);

    test.log_printf("Cross-MaxScale causal reads with causal_reads=universal");
    test_reads(test);

    test.log_printf("Master failure during universal causal read");
    test.check_maxctrl("alter service RW-Split-Router transaction_replay=true transaction_replay_timeout=60s");

    // The read-only versions will get errors as they try to insert inside of a read-only transaction which
    // always returns an error. We don't care as the main purpose is to stress-test the transaction replay
    // while causal_reads=universal is active.
    std::vector<std::thread> threads;
    threads.emplace_back(test_no_trx, std::ref(test));
    threads.emplace_back(test_autocommit_on, std::ref(test));
    threads.emplace_back(test_autocommit_off, std::ref(test));
    threads.emplace_back(test_rw_trx, std::ref(test));
    threads.emplace_back(test_ro_trx, std::ref(test));
    threads.emplace_back(test_ro_trx_set_trx, std::ref(test));

    for (int i = 0; i < 5; i++)
    {
        test.repl->block_node(0);
        test.maxscale->wait_for_monitor();
        sleep(5);
        test.repl->unblock_node(0);
        test.maxscale->wait_for_monitor();
        sleep(5);
    }

    running = false;

    for (auto& t : threads)
    {
        t.join();
    }

    auto conn = test.maxscale->rwsplit();
    conn.connect();

    for (int i = 1; i < id; i++)
    {
        conn.query("DROP TABLE test.t" + std::to_string(i));
    }

    test.repl->set_replication_delay(0);
    return test.global_result;
}
