/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-3663: Universal causal reads
 *
 * https://jira.mariadb.org/browse/MXS-3663
 */

#include <maxtest/testconnections.hh>
#include <maxsql/mariadb.hh>
#include <mysqld_error.h>
#include <set>

std::atomic<bool> running {true};
std::atomic<int> id{1};

void test_reads(TestConnections& test)
{
    std::string table = "test.t" + std::to_string(id++);
    auto conn = test.maxscale->rwsplit();
    conn.connect();
    test.expect(conn.query("CREATE OR REPLACE TABLE " + table + " (a INT)"),
                "[%d] Table creation should work: %s", conn.thread_id(), conn.error());
    conn.disconnect();

    auto secondary = test.maxscale->rwsplit();
    secondary.connect();
    auto id2 = secondary.thread_id();

    for (int i = 0; i < 10 && running && test.ok(); i++)
    {
        test.reset_timeout();
        conn.connect();
        test.expect(conn.query("INSERT INTO " + table + " VALUES ('" + std::to_string(i) + "')"),
                    "[%u <-> %u] INSERT should work: %s", conn.thread_id(), id2, conn.error());

        // Existing connections should also see the inserted rows
        auto count = atoi(secondary.field("SELECT COUNT(*) FROM " + table).c_str());
        test.expect(count == i + 1, "[%u <-> %u] Missing %d rows from open connection.",
                    conn.thread_id(), id2, (i + 1) - count);
        conn.disconnect();

        // New connections should see the inserted rows
        conn.connect();
        auto second_count = atoi(conn.field("SELECT COUNT(*) FROM " + table).c_str());
        test.expect(second_count == i + 1, "[%u <-> %u] Missing %d rows in second connection.",
                    conn.thread_id(), id2, (i + 1) - second_count);
        conn.disconnect();
    }
}

void check_row(TestConnections& test, const char* func, Connection& conn,
               const std::string& table, const std::string& value)
{
    auto stored_value = conn.field("SELECT MAX(a) FROM " + table + " WHERE a = " + value);
    test.expect(stored_value == value,
                "[%s] Row %s inserted by [%u] is wrong: %s%s%s",
                func, value.c_str(), conn.thread_id(),
                stored_value.empty() ? "<no stored value>" : stored_value.c_str(),
                stored_value.empty() ? " Error: " : "",
                conn.error());
}

void check_row_new_conn(TestConnections& test, const char* func, uint32_t orig_id,
                        const std::string& table, const std::string& value)
{
    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Failed to connect when querying '%s': %s", table.c_str(), conn.error());
    auto stored_value = conn.field("SELECT MAX(a) FROM " + table + " WHERE a = " + value);

    test.expect(stored_value == value,
                "[%s] Row %s inserted by [%u] is wrong for [%u]: %s%s%s",
                func, value.c_str(), orig_id, conn.thread_id(),
                stored_value.empty() ? "<no stored value>" : stored_value.c_str(),
                stored_value.empty() ? " Error: " : "",
                conn.error());
}

void ok_query(TestConnections& test, const char* func, Connection& conn,
              const std::string& sql)
{
    test.expect(conn.query(sql), "[%u] %s: Query '%s' failed: %s",
                conn.thread_id(), func, sql.c_str(), conn.error());
}

void maybe_ok_query(TestConnections& test, const char* func, Connection& conn,
                    const std::string& sql, std::set<int> accepted_errors)
{
    test.expect(conn.query(sql) || accepted_errors.find(conn.errnum()) != accepted_errors.end(),
                "[%u] %s: Query '%s' failed: %s", conn.thread_id(), func, sql.c_str(), conn.error());
}

void test_queries(TestConnections& test, const char* func,
                  std::function<void(Connection&, const std::string&, const std::string&)> cb)
{
    std::string table = "test.t" + std::to_string(id++);
    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "%s: Failed to connect: %s", func, conn.error());
    test.expect(conn.query("CREATE TABLE IF NOT EXISTS " + table + " (a INT PRIMARY KEY)"),
                "%s: Table creation should work: %u, %s", func, conn.thread_id(), conn.error());
    conn.disconnect();

    for (int i = 0; i < 100 && running && test.ok(); i++)
    {
        test.reset_timeout();
        conn.connect();

        // This should prevent leftover idle connections from holding locks on the database
        conn.query("SET wait_timeout=5");
        cb(conn, table, std::to_string(i));

        conn.disconnect();
    }
}

void test_no_trx(TestConnections& test)
{
    test_queries(test, __func__, [&](Connection& conn, const std::string& table, const std::string& value){
        maybe_ok_query(test, __func__, conn,
                       "INSERT INTO " + table + " VALUES ('" + value + "')",
                       {ER_DUP_ENTRY});
        check_row(test, __func__, conn, table, value);
        check_row_new_conn(test, __func__, conn.thread_id(), table, value);
    });
}

void test_rw_trx(TestConnections& test)
{
    test_queries(test, __func__, [&](Connection& conn, const std::string& table, const std::string& value){
        ok_query(test, __func__, conn, "START TRANSACTION");
        maybe_ok_query(test, __func__, conn,
                       "INSERT INTO " + table + " VALUES ('" + value + "')",
                       {ER_DUP_ENTRY});

        if (conn.query("COMMIT"))
        {
            check_row(test, __func__, conn, table, value);
            check_row_new_conn(test, __func__, conn.thread_id(), table, value);
        }
        else
        {
            test.expect(strstr(conn.error(), "Transaction checksum mismatch")
                        || mxq::mysql_is_net_error(conn.errnum()),
                        "Expected a replay failure or a network error: %s", conn.error());
        }
    });
}

void test_autocommit_on(TestConnections& test)
{
    test_queries(test, __func__, [&](Connection& conn, const std::string& table, const std::string& value){
        ok_query(test, __func__, conn, "SET autocommit=1");
        maybe_ok_query(test, __func__, conn,
                       "INSERT INTO " + table + " VALUES ('" + value + "')",
                       {ER_DUP_ENTRY});
        check_row(test, __func__, conn, table, value);
        check_row_new_conn(test, __func__, conn.thread_id(), table, value);
    });
}

void test_autocommit_off(TestConnections& test)
{
    test_queries(test, __func__, [&](Connection& conn, const std::string& table, const std::string& value){
        ok_query(test, __func__, conn, "SET autocommit=0");
        maybe_ok_query(test, __func__, conn,
                       "INSERT INTO " + table + " VALUES ('" + value + "')",
                       {ER_DUP_ENTRY});

        if (conn.query("COMMIT"))
        {
            check_row(test, __func__, conn, table, value);
            check_row_new_conn(test, __func__, conn.thread_id(), table, value);
        }
        else
        {
            test.expect(strstr(conn.error(), "Transaction checksum mismatch")
                        || mxq::mysql_is_net_error(conn.errnum()),
                        "Expected a replay failure or a network error: %s", conn.error());
        }
    });
}

void test_ro_trx(TestConnections& test)
{
    test_queries(test, __func__, [&](Connection& conn, const std::string& table, const std::string& value){
        ok_query(test, __func__, conn, "START TRANSACTION READ ONLY");
        maybe_ok_query(test, __func__, conn,
                       "INSERT INTO " + table + " VALUES ('" + value + "')",
                       {ER_CANT_EXECUTE_IN_READ_ONLY_TRANSACTION});
        // This should not fail even if the transaction gets replayed
        ok_query(test, __func__, conn, "COMMIT");
    });
}

void test_ro_trx_set_trx(TestConnections& test)
{
    test_queries(test, __func__, [&](Connection& conn, const std::string& table, const std::string& value){
        ok_query(test, __func__, conn, "SET TRANSACTION READ ONLY");
        ok_query(test, __func__, conn, "START TRANSACTION");
        maybe_ok_query(test, __func__, conn,
                       "INSERT INTO " + table + " VALUES ('" + value + "')",
                       {ER_CANT_EXECUTE_IN_READ_ONLY_TRANSACTION});
        // This should not fail even if the transaction gets replayed
        ok_query(test, __func__, conn, "COMMIT");
    });
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

    for (int i = 0; i < 5; i++)
    {
        threads.emplace_back(test_no_trx, std::ref(test));
        threads.emplace_back(test_autocommit_on, std::ref(test));
        threads.emplace_back(test_autocommit_off, std::ref(test));
        threads.emplace_back(test_rw_trx, std::ref(test));
        threads.emplace_back(test_ro_trx, std::ref(test));
        threads.emplace_back(test_ro_trx_set_trx, std::ref(test));
    }

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
