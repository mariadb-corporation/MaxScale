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
#include <maxtest/generate_sql.hh>
#include <maxtest/docker.hh>

#include "etl_common.hh"

void sanity_check(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    // By default the tables are created in the public schema of the user's own default database. In our case
    // the database name is maxskysql.
    if (test.expect(etl.query_odbc(dsn, "CREATE TABLE public.sanity_check(id INT)")
                    && etl.query_odbc(dsn, "INSERT INTO public.sanity_check VALUES (1), (2), (3)"),
                    "Failed to create tables in Postgres"))
    {
        auto [ok, res] = etl.run_etl(dsn, "server1", "postgresql", EtlTest::Op::START, 15s,
                                     {EtlTable {"public", "sanity_check"}});

        if (test.expect(ok, "ETL failed: %s", res.to_string().c_str()))
        {
            etl.compare_results(dsn, 0, "SELECT id FROM public.sanity_check ORDER BY id");
        }

        test.expect(etl.query_odbc(dsn, "DROP TABLE public.sanity_check")
                    && etl.query_native("server1", "DROP TABLE public.sanity_check"),
                    "Failed to drop tables in Postgres");
    }
}

void massive_result(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    if (test.expect(!!etl.query_odbc(dsn, "CREATE TABLE public.massive_result(id INT)"),
                    "Failed to create tables in Postgres"))
    {
        auto [ok, res] = etl.run_etl(dsn, "server1", "postgresql", EtlTest::Op::START, 150s,
                                 {EtlTable {"public", "massive_result",
                                            "CREATE OR REPLACE TABLE test.massive_result(id INT PRIMARY KEY) ENGINE=MEMORY",
                                            "SELECT 1 id FROM generate_series(0, 10000000)",
                                            "REPLACE INTO test.massive_result(id) VALUES (?)"
                                  }});

        test.expect(ok, "ETL failed: %s", res.to_string().c_str());

        test.expect(etl.query_odbc(dsn, "DROP TABLE public.massive_result")
                    && etl.query_native("server1", "DROP TABLE public.sanity_check"),
                    "Failed to drop tables in Postgres");
    }
}

void test_main(TestConnections& test)
{
    mxt::Docker docker(test, "postgres:14", "pg", {5432},
                       {"POSTGRES_USER=maxskysql", "POSTGRES_PASSWORD=skysql"},
                       "psql -U maxskysql -c \"SELECT 1\"");
    EtlTest etl(test);
    std::string dsn = "DRIVER=psqlodbcw.so;"
                      "UID=maxskysql;"
                      "PWD=skysql;"
                      "SERVER=127.0.0.1;"
                      "PORT=5432;";

    TestCases test_cases = {
        TESTCASE(sanity_check),
        TESTCASE(massive_result),
    };

    etl.run_tests(dsn, test_cases);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
