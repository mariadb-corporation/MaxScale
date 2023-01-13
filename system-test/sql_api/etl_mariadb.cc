/*
 * Copyright (c) 2023 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/http.hh>

#include "etl_common.hh"

void sanity_check(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    auto source = test.repl->get_connection(0);
    test.expect(source.connect()
                && source.query("CREATE TABLE test.etl_sanity_check(id INT)")
                && source.query("INSERT INTO test.etl_sanity_check SELECT seq FROM seq_0_to_10000"),
                "Failed to create test data");

    const char* SELECT = "SELECT COUNT(*) FROM test.etl_sanity_check";
    auto expected = source.field(SELECT);

    auto res = etl.run_etl(dsn, "server4", "mariadb", EtlTest::Op::START,
                           {EtlTable {"test", "etl_sanity_check"}});

    std::cout << res.to_string() << std::endl;

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

    auto res = etl.run_etl(dsn, "server4", "mariadb", EtlTest::Op::START,
                       {EtlTable {"test", "bad_sql",
                                  "CREATE TABLE test.bad_sql(id INT, a int)",
                                  "SELECT id FROM test.bad_sql",
                                  "INSERT INTO test.bad_sql(id, a) values (?, ?)"}});

    bool ok = true;
    test.expect(res.at("data/attributes/results").try_get_bool("ok", &ok) && !ok,
                "Bad SQL should cause ETL to fail: %s", res.to_string().c_str());

    std::cout << res.to_string() << std::endl;

    source.query("DROP TABLE test.bad_sql");
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    EtlTest etl(test);
    test.repl->stop_slaves();

    std::ostringstream ss;
    ss << "DRIVER=libmaodbc.so;"
       << "UID=" << test.repl->user_name() << ";"
       << "PWD=" << test.repl->password() << ";"
       << "SERVER=" << test.repl->ip(0) << ";"
       << "PORT=" << test.repl->port[0] << ";";
    std::string dsn = ss.str();

    test.log_printf("sanity_check");
    sanity_check(test, etl, dsn);

    test.log_printf("invalid_sql");
    invalid_sql(test, etl, dsn);

    return test.global_result;
}
