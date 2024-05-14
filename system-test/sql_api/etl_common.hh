/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxtest/testconnections.hh>
#include <maxbase/http.hh>
#include <maxbase/json.hh>
#include <maxbase/string.hh>
#include <maxbase/stopwatch.hh>

#include <map>

#define TESTCASE(a) {a, #a}

class EtlTest;
using TestFn = void (*)(TestConnections&, EtlTest&, const std::string&);
using TestCases = std::vector<std::pair<TestFn, const char*>>;

struct EtlTable
{
    EtlTable(std::string_view sch,
             std::string_view tab,
             std::string_view cre = "",
             std::string_view sel = "",
             std::string_view ins = "")
        : schema(sch)
        , table(tab)
        , create(cre)
        , select(sel)
        , insert(ins)
    {
    }

    std::string schema;
    std::string table;
    std::string create;
    std::string select;
    std::string insert;
};

class EtlTest
{
public:
    struct SqlApiConn
    {
        std::string id;
        std::string token;
    };

    struct EtlJob
    {
        SqlApiConn source;
        SqlApiConn dest;
        mxb::Json  request{mxb::Json::Type::UNDEFINED};
        mxb::Json  response{mxb::Json::Type::UNDEFINED};
    };

    enum class Op
    {
        PREPARE,
        START,
    };

    enum class Mode
    {
        NORMAL,
        REPLACE,
        IGNORE,
    };

    EtlTest(TestConnections& test)
        : m_test(test)
    {
        static mxb::http::Init initer;

        // TODO: Replace this with something universal
        auto res = m_test.maxscale->ssh_output("yum -y install postgresql-odbc");
        m_test.expect(res.rc == 0, "Failed to install ODBC drivers: %s", res.output.c_str());
    }

    void run_tests(const std::string& dsn, const TestCases& test_cases)
    {
        for (const auto& [fn, name] : test_cases)
        {
            if (m_test.ok())
            {
                m_test.log_printf("%s", name);
                fn(m_test, *this, dsn);
                m_test.reset_timeout();
            }
        }
    }

    mxb::http::Response get(std::string_view endpoint)
    {
        return mxb::http::get(url(endpoint), "admin", "mariadb");
    }

    mxb::http::Response del(std::string_view endpoint)
    {
        return mxb::http::del(url(endpoint), "", "admin", "mariadb");
    }

    mxb::http::Response post(std::string_view endpoint, mxb::Json js)
    {
        return mxb::http::post(url(endpoint), js.to_string(mxb::Json::Format::COMPACT), "admin", "mariadb");
    }

    mxb::Json connect(const std::map<std::string, std::string>& values)
    {
        mxb::Json js(mxb::Json::Type::OBJECT);

        for (const auto& [k, v] : values)
        {
            js.set_string(k.c_str(), v);
        }

        auto res = post("sql", js);
        m_test.expect(res.code == 201, "POST to /sql returned %d: %s", res.code, res.body.c_str());

        js.reset();
        m_test.expect(js.load_string(res.body), "Malformed JSON in response: %s", res.body.c_str());

        return js;
    }

    mxb::Json query(const std::map<std::string, std::string>& params, const std::string& sql)
    {
        mxb::Json rval(mxb::Json::Type::UNDEFINED);

        if (auto conn = connect(params))
        {
            auto id = conn.at("data/id").get_string();
            auto token = conn.at("meta/token").get_string();

            mxb::Json payload(mxb::Json::Type::OBJECT);
            payload.set_string("sql", sql);
            auto res = post(mxb::cat("sql/", id, "/queries/?token=", token), payload);
            rval.load_string(res.body);

            del("sql/" + id + "?token=" + token);
        }

        return rval;
    }

    mxb::Json query_odbc(const std::string& dsn, const std::string& sql)
    {
        return query({
            {"target", "odbc"},
            {"connection_string", dsn}
        }, sql);
    }

    mxb::Json query_native(const std::string& server, const std::string& sql)
    {
        return query({
            {"target", server},
            {"user", m_test.maxscale->user_name()},
            {"password", m_test.maxscale->password()}
        }, sql);
    }

    bool compare_results(const std::string& dsn, int node,
                         const std::string& sql_src,
                         const std::string& sql_dest)
    {
        std::ostringstream ss;
        ss << "DRIVER=libmaodbc.so;"
           << "SERVER=" << m_test.repl->ip(node) << ";"
           << "PORT=" << m_test.repl->port(node) << ";"
           << "UID=" << m_test.maxscale->user_name() << ";"
           << "PWD={" << m_test.maxscale->password() << "}";

        auto source = query_odbc(dsn, sql_src).at("data/attributes/results");

        // The connection requires some setup to be usable with the same SQL on both the source and the
        // destination. The most important of these is SQL_MODE=ANSI_QUOTES which makes MariaDB behave like
        // other databases when it comes to quoting identifiers.
        mxb::Json dest(mxb::Json::Type::UNDEFINED);

        if (auto conn = connect({
            {"target", "odbc"},
            {"connection_string", ss.str()}
        }))
        {
            auto id = conn.at("data/id").get_string();
            auto token = conn.at("meta/token").get_string();

            mxb::Json payload(mxb::Json::Type::OBJECT);
            payload.set_string("sql", "SET SQL_MODE='ANSI_QUOTES'");
            post(mxb::cat("sql/", id, "/queries/?token=", token), payload);

            payload.set_string("sql", sql_dest);
            auto res = post(mxb::cat("sql/", id, "/queries/?token=", token), payload);
            mxb::Json js(mxb::Json::Type::UNDEFINED);

            if (js.load_string(res.body))
            {
                dest = js.at("data/attributes/results");

                // Remove the connection metadata, this is different between databases and can't be compared.
                source.set_null("metadata");
                dest.set_null("metadata");
            }

            del("sql/" + id + "?token=" + token);
        }

        for (mxb::Json obj : source.get_array_elems())
        {
            obj.erase("metadata");
        }

        for (mxb::Json obj : dest.get_array_elems())
        {
            obj.erase("metadata");
        }

        return m_test.expect(source.valid() == dest.valid() && source == dest,
                             "Result mismatch for '%s'. Source %s\n Destination: %s",
                             sql_dest.c_str(), source.to_string().c_str(), dest.to_string().c_str());
    }

    bool compare_results(const std::string& dsn, int node, const std::string& sql)
    {
        return compare_results(dsn, node, sql, sql);
    }

    // Checks that a query did not return an error
    void check_odbc_result(const std::string& dsn, const std::string& sql)
    {
        auto res = query_odbc(dsn, sql);
        m_test.expect(res.at("data/attributes/results/0/errno").get_int() <= 0,
                      "Failed execute query '%s': %s",
                      sql.c_str(), res.to_string().c_str());
    }

    EtlJob prepare_etl(std::string source_dsn, std::string destination, std::string type,
                       std::chrono::seconds timeout, std::vector<EtlTable> tables, Mode mode)
    {
        auto source = connect({
            {"target", "odbc"},
            {"connection_string", source_dsn},
            {"timeout", std::to_string(timeout.count())},
        });

        m_test.expect(source.valid(), "Failed to create source connection");

        auto dest = connect({
            {"target", destination},
            {"user", m_test.maxscale->user_name()},
            {"password", m_test.maxscale->password()},
            {"timeout", std::to_string(timeout.count())},
        });

        m_test.expect(dest.valid(), "Failed to create destination connection");

        EtlJob job;
        job.source.id = source.at("data/id").get_string();
        job.source.token = source.at("meta/token").get_string();

        job.dest.id = dest.at("data/id").get_string();
        job.dest.token = dest.at("meta/token").get_string();

        mxb::Json js(mxb::Json::Type::OBJECT);
        js.set_string("type", type);
        js.set_string("target", job.dest.id);
        js.set_int("timeout", timeout.count());

        for (const auto& [k, v] : m_extra)
        {
            js.set_string(k.c_str(), v);
        }

        if (mode == Mode::REPLACE)
        {
            js.set_string("create_mode", "replace");
        }
        else if (mode == Mode::IGNORE)
        {
            js.set_string("create_mode", "ignore");
        }

        for (const auto& t : tables)
        {
            mxb::Json elem(mxb::Json::Type::OBJECT);
            elem.set_string("table", t.table);
            elem.set_string("schema", t.schema);

            if (!t.create.empty())
            {
                elem.set_string("create", t.create);
            }

            if (!t.select.empty())
            {
                elem.set_string("select", t.select);
            }

            if (!t.insert.empty())
            {
                elem.set_string("insert", t.insert);
            }

            js.add_array_elem("tables", std::move(elem));
        }

        job.request = js;
        return job;
    }

    void start_etl(EtlJob& job, Op operation)
    {
        auto etl_url = mxb::cat("sql/", job.source.id, "/etl/",
                                operation == Op::PREPARE ? "prepare" : "start",
                                "?token=", job.source.token,
                                "&target_token=", job.dest.token);

        job.response.load_string(post(etl_url, job.request).body);
    }

    void wait_for_etl(EtlJob& job, std::chrono::seconds timeout)
    {
        auto self = job.response.at("links/self").get_string();
        self += "?token=" + job.source.token;
        auto start = mxb::Clock::now();
        auto sleep_time = 100ms;
        mxb::http::Response res;

        do
        {
            if (mxb::Clock::now() - start < std::chrono::seconds(timeout))
            {
                std::this_thread::sleep_for(sleep_time);
                sleep_time = std::min(sleep_time * 2, std::chrono::milliseconds {5000});
            }
            else
            {
                m_test.add_failure("ETL timed out");
            }

            // Use a raw mxb::http:get(), the `self` already includes the hostname and port.
            res = mxb::http::get(self, "admin", "mariadb");
            job.response.reset();
            job.response.load_string(res.body);
        }
        while (res.code == 202 && m_test.ok());

        if (res.code != 201)
        {
            m_test.tprintf("ETL failed:\n%s", res.body.c_str());
            job.response.reset();
        }
    }

    void stop_etl(EtlJob& job)
    {
        del(mxb::cat("sql/", job.source.id, "?token=", job.source.token));
        del(mxb::cat("sql/", job.dest.id, "?token=", job.dest.token));
    }

    void cancel_etl(EtlJob& job)
    {
        post(mxb::cat("sql/", job.source.id, "/cancel?token=", job.source.token),
             mxb::Json {mxb::Json::Type::OBJECT});
    }

    std::pair<bool, mxb::Json> run_etl(std::string source_dsn,
                                       std::string destination,
                                       std::string type,
                                       Op operation,
                                       std::chrono::seconds timeout,
                                       std::vector<EtlTable> tables,
                                       Mode mode = Mode::NORMAL)
    {

        EtlJob job = prepare_etl(source_dsn, destination, type, timeout, tables, mode);
        start_etl(job, operation);
        wait_for_etl(job, timeout);

        bool ok = false;
        job.response.at("data/attributes/results").try_get_bool("ok", &ok);

        stop_etl(job);

        return {ok, job.response};
    }

    void set_extra(std::map<std::string, std::string> extras)
    {
        m_extra = std::move(extras);
    }

    TestConnections& test()
    {
        return m_test;
    }

private:

    std::string url(std::string_view endpoint)
    {
        return mxb::cat("http://", m_test.maxscale->ip(), ":8989/v1/", endpoint);
    }

    TestConnections&                   m_test;
    std::map<std::string, std::string> m_extra;
};
