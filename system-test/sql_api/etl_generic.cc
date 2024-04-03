/*
 * Copyright (c) 2023 MariaDB plc
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

#include <maxtest/testconnections.hh>
#include <maxtest/docker.hh>

#include "etl_common.hh"

void sanity_check(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    if (test.expect(etl.query_odbc(dsn, "CREATE TABLE test.sanity_check(id INT)")
                    && etl.query_odbc(dsn, "INSERT INTO test.sanity_check VALUES (1), (2), (3)"),
                    "Failed to create tables in Postgres"))
    {
        auto [ok, res] = etl.run_etl(dsn, "server1", "generic", EtlTest::Op::START, 15s,
                                     {EtlTable {"test", "sanity_check"}});

        if (test.expect(ok, "ETL failed: %s", res.to_string().c_str()))
        {
            etl.compare_results(dsn, 0, "SELECT id FROM test.sanity_check ORDER BY id");
        }

        test.expect(etl.query_odbc(dsn, "DROP TABLE test.sanity_check")
                    && etl.query_native("server1", "DROP TABLE test.sanity_check"),
                    "Failed to drop tables in Postgres");
    }
}

void test_main(TestConnections& test)
{
    mxt::Docker docker(test, "postgres:14", "pg", {5432},
                       {"POSTGRES_USER=maxskysql", "POSTGRES_PASSWORD=skysql"},
                       "",
                       "psql -U maxskysql -c \"SELECT 1\"");
    EtlTest etl(test);
    std::string dsn = "DRIVER=psqlodbcw.so;"
                      "UID=maxskysql;"
                      "PWD=skysql;"
                      "SERVER=127.0.0.1;"
                      "PORT=5432;"
                      "BoolsAsChar=0;";

    TestCases test_cases = {
        TESTCASE(sanity_check),
    };

    etl.check_odbc_result(dsn, "CREATE SCHEMA test");
    etl.set_extra({{"catalog", "maxskysql"}});
    etl.run_tests(dsn, test_cases);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
