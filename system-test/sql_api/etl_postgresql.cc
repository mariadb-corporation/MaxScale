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
#include <maxbase/http.hh>
#include <maxtest/generate_sql.hh>
#include <maxtest/docker.hh>

#include "etl_common.hh"
namespace
{
void compare_values(EtlTest& etl, const std::string& dsn, const sql_generation::SQLType& t)
{
    if (t.type_name == "TIMESTAMP")
    {
        std::string TIMESTAMP_SELECT =
            "SELECT "
            "CAST(EXTRACT(YEAR FROM a) AS INT) y, "
            "CAST(EXTRACT(MONTH FROM a) AS INT) m, "
            "CAST(EXTRACT(DAY FROM a) AS INT) d, "
            "CAST(EXTRACT(HOUR FROM a) AS INT) h, "
            "CAST(EXTRACT(MINUTE FROM a) AS INT) min, "
            "CAST(EXTRACT(SECOND FROM a) AS INT) sec "
            " FROM " + t.full_name;

        etl.compare_results(dsn, 0, TIMESTAMP_SELECT);
    }
    else if (t.type_name == "UUID")
    {
        etl.compare_results(dsn, 0, "SELECT LOWER(CAST(a AS VARCHAR(200))) uuid_lower FROM " + t.full_name);
    }
    else
    {
        etl.compare_results(dsn, 0, "SELECT * FROM " + t.full_name);
    }
}

std::string big_number(int n, int d)
{
    mxb_assert(d < n);
    std::string rval(n + (d ? 1 : 0), '0');
    rval.front() = '1';
    rval[n - d] = '.';
    rval.back() = '1';
    return rval;
}

std::string_view unquote(std::string_view str)
{
    return str.size() >= 2 && str.front() == '\'' && str.back() == '\'' ?
           str.substr(1, str.size() - 2) : str;
}

void run_simple_test(EtlTest& etl, const std::string& dsn, std::string schema, std::string table,
                     std::string create, std::string insert, std::string select, std::string drop)
{
    etl.check_odbc_result(dsn, create);
    etl.check_odbc_result(dsn, insert);

    auto [ok, res] = etl.run_etl(dsn, "server1", "postgresql", EtlTest::Op::START, 15s,
                                 {EtlTable {schema, table}});

    if (etl.test().expect(ok, "ETL failed: %s", res.to_string().c_str()))
    {
        etl.compare_results(dsn, 0, select);
    }

    auto dest = etl.test().repl->get_connection(0);
    etl.test().expect(dest.connect(), "Failed to connect to node 0: %s", dest.error());

    etl.check_odbc_result(dsn, drop);
    dest.query(drop);
}
}

void sanity_check(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    // By default the tables are created in the public schema of the user's own default database. In our case
    // the database name is maxskysql.
    run_simple_test(etl, dsn, "public", "sanity_check",
                    "CREATE TABLE public.sanity_check(id INT)",
                    "INSERT INTO public.sanity_check VALUES (1), (2), (3)",
                    "SELECT id FROM public.sanity_check ORDER BY id",
                    "DROP TABLE public.sanity_check");
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
                compare_values(etl, dsn, t);
            }

            etl.check_odbc_result(dsn, t.drop_sql);
            test.expect(dest.query(t.drop_sql), "Failed to drop: %s", dest.error());
        }
    }
}

void test_parallel_datatypes(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    auto dest = test.repl->get_connection(0);
    test.expect(dest.connect(), "Failed to connect to node 0: %s", dest.error());
    dest.query("SET SQL_MODE='ANSI_QUOTES'");

    std::vector<EtlTable> tables;

    for (const auto& t : sql_generation::postgres_types())
    {

        etl.check_odbc_result(dsn, t.create_sql);

        for (const auto& val : t.values)
        {
            etl.check_odbc_result(dsn, val.insert_sql);
        }


        tables.emplace_back(t.database_name, t.table_name);
    }


    auto [ok, res] = etl.run_etl(dsn, "server1", "postgresql", EtlTest::Op::START, 15s,
                                 tables);

    test.expect(ok, "ETL failed: %s", res.to_string().c_str());

    for (const auto& t : sql_generation::postgres_types())
    {
        compare_values(etl, dsn, t);
        etl.check_odbc_result(dsn, t.drop_sql);
        test.expect(dest.query(t.drop_sql), "Failed to drop: %s", dest.error());
    }
}

void big_numbers(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    std::string insert;

    for (int i = 1; i < (65 - 38) && test.ok(); i++)
    {
        for (int d = 0; d <= 38 && d < i && test.ok(); d++)
        {
            insert += "INSERT INTO public.big_numbers VALUES (" + big_number(i, d) + ");";
        }
    }

    // The arguments to DECIMAL are the precision and the scale: the total amount of numbers on both sides of
    // the decimal point and how many numbers can appear after the decimal point.
    run_simple_test(etl, dsn, "public", "big_numbers",
                    "CREATE TABLE public.big_numbers(a DECIMAL(65,38))",
                    insert,
                    "SELECT * FROM public.big_numbers",
                    "DROP TABLE public.big_numbers");
}

void default_values(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    auto insert = "INSERT INTO public.default_values(a, b, c) VALUES "
                  "(1, 1, 3), (2, DEFAULT, DEFAULT), (3, NULL, NULL), (4, 4, 4)";
    run_simple_test(etl, dsn, "public", "default_values",
                    "CREATE TABLE public.default_values(a INT, b INT DEFAULT 4, c INT DEFAULT NULL)",
                    insert,
                    "SELECT * FROM public.default_values",
                    "DROP TABLE public.default_values");
}

void generated_columns(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    run_simple_test(etl, dsn, "public", "generated_columns",
                    "CREATE TABLE public.generated_columns(a INT, b INT GENERATED ALWAYS AS (a + 1) STORED)",
                    "INSERT INTO public.generated_columns(a) VALUES (1), (2), (NULL), (0), (-1)",
                    "SELECT * FROM public.generated_columns",
                    "DROP TABLE public.generated_columns");
}

void sequences(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    // We need to pre-create the sequence in MariaDB in order for it to work.
    auto dest = test.repl->get_connection(0);
    test.expect(dest.connect(), "Failed to connect to node 0: %s", dest.error());
    dest.query("CREATE DATABASE IF NOT EXISTS public;CREATE SEQUENCE public.s1;");

    run_simple_test(etl, dsn, "public", "sequences",
                    "CREATE SEQUENCE s1; CREATE TABLE public.sequences(a INT, b INT DEFAULT NEXTVAL('s1'))",
                    "INSERT INTO public.sequences(a) SELECT generate_series(0, 1000)",
                    "SELECT * FROM public.sequences",
                    "DROP TABLE public.sequences");

    dest.query("DROP SEQUENCE public.s1");
}

void user_defined_types(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    auto create =
        R"(
CREATE TYPE my_type AS (a int, b text, c real);
CREATE TABLE user_defined_types(a my_type, b my_type);
INSERT INTO user_defined_types VALUES ((1, 'hello', 3), (2, 'world', 4));
    )";

    etl.check_odbc_result(dsn, create);

    auto [ok, res] = etl.run_etl(dsn, "server1", "postgresql", EtlTest::Op::START, 15s,
                                 {EtlTable {"public", "user_defined_types"}});

    if (test.expect(ok, "ETL failed: %s", res.to_string().c_str()))
    {
        etl.compare_results(dsn, 0, "SELECT TO_JSON(a) a, TO_JSON(b) b FROM public.user_defined_types",
                            "SELECT a, b FROM public.user_defined_types");
    }

    etl.check_odbc_result(dsn, "DROP TABLE public.user_defined_types; DROP TYPE my_type;");

    auto dest = test.repl->get_connection(0);
    test.expect(dest.connect(), "Failed to connect to node 0: %s", dest.error());
    test.expect(dest.query("DROP TABLE public.user_defined_types;"), "Failed to drop: %s", dest.error());
}

void array_types(TestConnections& test, EtlTest& etl, const std::string& dsn)
{
    auto create =
        R"(
CREATE TABLE array_type(a int[], b text[]);
INSERT INTO array_type VALUES ('{1, 2, 3}', '{''hello'', ''world''}');
    )";

    etl.check_odbc_result(dsn, create);

    auto [ok, res] = etl.run_etl(dsn, "server1", "postgresql", EtlTest::Op::START, 15s,
                                 {EtlTable {"public", "array_type"}});

    if (test.expect(ok, "ETL failed: %s", res.to_string().c_str()))
    {
        etl.compare_results(dsn, 0, "SELECT TO_JSON(a) a, TO_JSON(b) b FROM public.array_type",
                            "SELECT a, b FROM public.array_type");
    }

    etl.check_odbc_result(dsn, "DROP TABLE public.array_type;");

    auto dest = test.repl->get_connection(0);
    test.expect(dest.connect(), "Failed to connect to node 0: %s", dest.error());
    test.expect(dest.query("DROP TABLE public.array_type;"), "Failed to drop: %s", dest.error());
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
        TESTCASE(massive_result),
        TESTCASE(test_datatypes),
        TESTCASE(test_parallel_datatypes),
        TESTCASE(big_numbers),
        TESTCASE(default_values),
        TESTCASE(generated_columns),
        TESTCASE(sequences),
        TESTCASE(user_defined_types),
        TESTCASE(array_types),
    };

    etl.check_odbc_result(dsn, "CREATE SCHEMA test");

    etl.run_tests(dsn, test_cases);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
