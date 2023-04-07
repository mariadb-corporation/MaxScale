/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/http.hh>
#include <maxtest/generate_sql.hh>

#include "etl_common.hh"

namespace
{
void compare_results(TestConnections& test, Connection& source_conn, Connection& dest_conn,
                     const std::string& sql, const sql_generation::SQLType& type)
{
    auto source = source_conn.rows(sql);
    auto dest = dest_conn.rows(sql);

    if (test.expect(!source.empty() && !dest.empty(),
                    "Both should return a result: source reports %s, dest reports %s",
                    source_conn.error(), dest_conn.error()))
    {

        if (test.expect(source.size() == dest.size(),
                        "Result size mismatch: source has %lu rows whereas dest has %lu",
                        source.size(), dest.size()))
        {
            for (size_t i = 0; i < source.size(); i++)
            {
                if (test.expect(source[i].size() == dest[i].size(),
                                "Row at offset %lu has a column count mismatch: "
                                "source has %lu columns whereas dest has %lu",
                                i, source[i].size(), dest[i].size()))
                {
                    for (size_t c = 0; c < source[i].size(); c++)
                    {
                        test.expect(source[i][c] == dest[i][c],
                                    "Column %lu for row at offset %lu does not match: "
                                    "source has '%s' whereas dest has '%s'",
                                    c, i, source[i][c].c_str(), dest[i][c].c_str());
                    }
                }
            }
        }
    }
}
}

void sanity_check(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    auto source = test.repl->get_connection(0);
    test.expect(source.connect()
                && source.query("CREATE TABLE test.etl_sanity_check(id INT)")
                && source.query("INSERT INTO test.etl_sanity_check SELECT seq FROM seq_0_to_10000"),
                "Failed to create test data");

    const char* SELECT = "SELECT COUNT(*) FROM test.etl_sanity_check";
    auto expected = source.field(SELECT);

    auto [ok, res] = etl.run_etl(dsn, "server4", "mariadb", EtlTest::Op::START, 15s,
                                 {EtlTable {"test", "etl_sanity_check"}});

    test.expect(ok, "ETL failed: %s", res.to_string().c_str());

    auto dest = test.repl->get_connection(3);
    dest.connect();
    auto result = dest.field(SELECT);

    test.expect(result == expected, "Expected '%s' rows but got '%s' (error: %s)",
                expected.c_str(), result.c_str(), dest.error());

    source.query("DROP TABLE test.etl_sanity_check");
    dest.query("DROP TABLE test.etl_sanity_check");
}

void invalid_sql(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    auto source = test.repl->get_connection(0);
    test.expect(source.connect()
                && source.query("CREATE TABLE test.bad_sql(id INT)")
                && source.query("INSERT INTO test.bad_sql SELECT seq FROM seq_0_to_100"),
                "Failed to create test data");

    auto [ok, res] = etl.run_etl(dsn, "server4", "mariadb", EtlTest::Op::START, 15s,
                             {EtlTable {"test", "bad_sql",
                                        "CREATE TABLE test.bad_sql(id INT, a int)",
                                        "SELECT id FROM test.bad_sql",
                                        "INSERT INTO test.bad_sql(id, a) values (?, ?)"}});

    test.expect(!ok, "Bad SQL should cause ETL to fail: %s", res.to_string().c_str());

    source.query("DROP TABLE test.bad_sql");
}

void reuse_connections(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    auto source = test.repl->get_connection(0);
    test.expect(source.connect()
                && source.query("CREATE TABLE test.reuse_connections(id INT)")
                && source.query("INSERT INTO test.reuse_connections SELECT seq FROM seq_0_to_100"),
                "Failed to create test data");

    auto job = etl.prepare_etl(dsn, "server4", "mariadb", 15s,
                               {EtlTable {"test", "reuse_connections"}},
                               EtlTest::Mode::REPLACE);

    for (int i = 0; i < 50; i++)
    {
        etl.start_etl(job, EtlTest::Op::START);
        etl.wait_for_etl(job, 15s);

        bool ok = false;
        job.response.at("data/attributes/results").try_get_bool("ok", &ok);
        test.expect(ok, "ETL failed: %s", job.response.to_string().c_str());
        etl.compare_results(dsn, 3, "SELECT COUNT* FROM test.reuse_connections");
    }

    etl.stop_etl(job);

    source.query("DROP TABLE test.reuse_connections");

    auto dest = test.repl->get_connection(3);
    dest.connect();
    dest.query("DROP TABLE test.reuse_connections");
}

void test_datatypes(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    auto source = test.repl->get_connection(0);
    test.expect(source.connect(), "Failed to connect to node 0: %s", source.error());
    auto dest = test.repl->get_connection(3);
    dest.connect();

    for (const auto& t : sql_generation::mariadb_types())
    {
        for (const auto& val : t.values)
        {
            test.expect(source.query(t.create_sql), "Failed to create table: %s", source.error());
            test.expect(source.query(val.insert_sql), "Failed to insert into table: %s", source.error());
            auto [ok, res] = etl.run_etl(dsn, "server4", "mariadb", EtlTest::Op::START, 15s,
                                         {EtlTable {t.database_name, t.table_name}});

            if (test.expect(ok, "ETL failed for %s %s: %s", t.type_name.c_str(), val.value.c_str(),
                            res.to_string().c_str()))
            {
                compare_results(test, source, dest, "SELECT * FROM " + t.full_name, t);
                etl.compare_results(dsn, 3, "SELECT * FROM " + t.full_name);
            }

            source.query(t.drop_sql);
            dest.query(t.drop_sql);
        }
    }
}

void test_parallel_datatypes(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    auto source = test.repl->get_connection(0);
    test.expect(source.connect(), "Failed to connect to node 0: %s", source.error());
    auto dest = test.repl->get_connection(3);
    dest.connect();

    std::vector<EtlTable> tables;

    for (const auto& t : sql_generation::mariadb_types())
    {
        test.expect(source.query(t.create_sql), "Failed to create table: %s", source.error());

        for (const auto& val : t.values)
        {
            test.expect(source.query(val.insert_sql), "Failed to insert into table: %s", source.error());
        }

        tables.emplace_back(t.database_name, t.table_name);
    }


    auto [ok, res] = etl.run_etl(dsn, "server4", "mariadb", EtlTest::Op::START, 15s, tables);

    test.expect(ok, "ETL failed: %s", res.to_string().c_str());

    for (const auto& t : sql_generation::mariadb_types())
    {
        compare_results(test, source, dest, "SELECT * FROM " + t.full_name, t);
        etl.compare_results(dsn, 3, "SELECT * FROM " + t.full_name);
        source.query(t.drop_sql);
        dest.query(t.drop_sql);
    }
}

void massive_result(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    // We'll need a table so that the coordinator thread can lock it.
    const char* TABLE_DEF = "CREATE TABLE test.massive_result(id VARCHAR(1024) PRIMARY KEY) ENGINE=MEMORY";

    auto source = test.repl->get_connection(0);
    test.expect(source.connect() && source.query(TABLE_DEF),
                "Failed to create dummy table: %s", source.error());

    auto [ok, res] = etl.run_etl(
        dsn, "server4", "mariadb", EtlTest::Op::START, 150s,
        {EtlTable {
             "test", "massive_result",
             "",    // If left empty, the ETL will read the CREATE TABLE statement from the server
             "SELECT REPEAT('a', 1000) FROM test.seq_0_to_1000000",
             "REPLACE INTO test.massive_result(id) VALUES (?)"
         }});

    test.expect(ok, "ETL failed: %s", res.to_string().c_str());

    auto dest = test.repl->get_connection(3);
    dest.connect();
    source.query("DROP TABLE test.massive_result");
    dest.query("DROP TABLE test.massive_result");
}

void cancel_etl(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    etl.check_odbc_result(dsn, "CREATE TABLE test.cancel_etl(id INT)");
    std::string sql = "INSERT INTO test.cancel_etl VALUES (1)";

    for (int i = 0; i < 300; i++)
    {
        sql += ",(1)";
    }

    etl.check_odbc_result(dsn, sql);

    const char* INSERT = "INSERT INTO test.cancel_etl(id) VALUES (SLEEP(?))";
    auto job = etl.prepare_etl(dsn, "server4", "mariadb", 15s,
                               {EtlTable {"test", "cancel_etl", "", "", INSERT}},
                               EtlTest::Mode::NORMAL);

    etl.start_etl(job, EtlTest::Op::START);

    // Wait for a few seconds and then cancel the ETL
    sleep(3);
    etl.cancel_etl(job);

    etl.wait_for_etl(job, 15s);

    bool ok = false;
    job.response.at("data/attributes/results").try_get_bool("ok", &ok);
    test.expect(!ok, "ETL should fail: %s", job.response.to_string().c_str());

    etl.stop_etl(job);
    etl.query_odbc(dsn, "DROP TABLE test.cancel_etl");
    etl.query_native("server4", "DROP TABLE test.cancel_etl");
}

void test_main(TestConnections& test)
{
    EtlTest etl(test);
    test.repl->stop_slaves();

    std::ostringstream ss;
    ss << "DRIVER=libmaodbc.so;"
       << "UID=" << test.repl->user_name() << ";"
       << "PWD=" << test.repl->password() << ";"
       << "SERVER=" << test.repl->ip(0) << ";"
       << "PORT=" << test.repl->port[0] << ";";

    TestCases test_cases = {
        TESTCASE(sanity_check),
        TESTCASE(invalid_sql),
        TESTCASE(reuse_connections),
        TESTCASE(test_datatypes),
        TESTCASE(test_parallel_datatypes),
        TESTCASE(massive_result),
        TESTCASE(cancel_etl),
    };

    etl.run_tests(ss.str(), test_cases);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
