/*
 * Copyright (c) 2023 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
namespace
{
std::string_view unquote(std::string_view str)
{
    return str.size() >= 2 && str.front() == '\'' && str.back() == '\'' ?
           str.substr(1, str.size() - 2) : str;
}
}

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

void test_datatypes(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    etl.check_odbc_result(dsn, "CREATE SCHEMA test");
    auto dest = test.repl->get_connection(0);
    test.expect(dest.connect(), "Failed to connect to node 0: %s", dest.error());
    dest.query("SET SQL_MODE='ANSI_QUOTES'");

    for (const auto& t : sql_generation::postgres_types())
    {
        for (const auto& val : t.values)
        {
            etl.check_odbc_result(dsn, t.create_sql);
            etl.check_odbc_result(dsn, val.insert_sql);

            auto [ok, res] = etl.run_etl(dsn, "server1", "postgresql", EtlTest::Op::START, 15s,
                                         {EtlTable {t.database_name, t.table_name}});

            if (test.expect(ok, "ETL failed for %s %s: %s", t.type_name.c_str(), val.value.c_str(),
                            res.to_string().c_str()))
            {
                if (t.type_name == "TIMESTAMP")
                {
                    // MariaDB formats datetime values with trailing zeroes differently than Postgres does and
                    // thus the results will never match. For now, just check that the values in the database
                    // is what we expected to add there.
                    auto inserted_val = dest.field("SELECT * FROM " + t.full_name);
                    std::string raw_value(unquote(val.value));

                    test.expect(inserted_val.find(raw_value) != std::string::npos
                                || (inserted_val.empty() && val.value == "NULL"),
                                "TIMESTAMP mismatch: %s != %s", raw_value.c_str(), inserted_val.c_str());
                }
                else
                {
                    etl.compare_results(dsn, 0, "SELECT * FROM " + t.full_name);
                }
            }

            etl.check_odbc_result(dsn, t.drop_sql);
            test.expect(dest.query(t.drop_sql), "Failed to drop: %s", dest.error());
        }
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
                      "PORT=5432;"
                      "BoolsAsChar=0;";

    TestCases test_cases = {
        TESTCASE(sanity_check),
        TESTCASE(massive_result),
        TESTCASE(test_datatypes),
    };

    etl.run_tests(dsn, test_cases);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
