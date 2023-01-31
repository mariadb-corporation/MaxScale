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
#pragma once

#include <maxtest/testconnections.hh>
#include <maxbase/http.hh>
#include <maxbase/json.hh>
#include <maxbase/string.hh>
#include <maxbase/stopwatch.hh>

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

    bool compare_results(const std::string& dsn, int node, const std::string& sql)
    {
        std::ostringstream ss;
        ss << "DRIVER=libmaodbc.so;"
           << "SERVER=" << m_test.repl->ip(node) << ";"
           << "PORT=" << m_test.repl->port[node] << ";"
           << "UID=" << m_test.maxscale->user_name() << ";"
           << "PWD={" << m_test.maxscale->password() << "}";

        auto source = query_odbc(dsn, sql).at("data/attributes/results");
        auto dest = query_odbc(ss.str(), sql).at("data/attributes/results");

        return m_test.expect(source.valid() == dest.valid() && source == dest,
                             "Result mismatch. Source %s\n Destination: %s",
                             source.to_string().c_str(), dest.to_string().c_str());
    }

    std::pair<bool, mxb::Json> run_etl(std::string source_dsn,
                                       std::string destination,
                                       std::string type,
                                       Op operation,
                                       std::chrono::seconds timeout,
                                       std::vector<EtlTable> tables,
                                       Mode mode = Mode::NORMAL,
                                       int iterations = 1)
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

        auto source_id = source.at("data/id").get_string();
        auto source_token = source.at("meta/token").get_string();

        auto dest_id = dest.at("data/id").get_string();
        auto dest_token = dest.at("meta/token").get_string();

        mxb::Json js(mxb::Json::Type::OBJECT);
        js.set_string("type", type);
        js.set_string("target", dest_id);
        js.set_int("timeout", timeout.count());

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

        auto etl_url = mxb::cat("sql/", source_id, "/etl/",
                                operation == Op::PREPARE ? "prepare" : "start",
                                "?token=", source_token,
                                "&target_token=", dest_token);
        bool ok = false;
        mxb::Json response;

        for (int i = 0; i < iterations; i++)
        {
            auto res = post(etl_url, js);
            response.load_string(res.body);
            auto self = response.at("links/self").get_string();
            self += "?token=" + source_token;
            auto start = mxb::Clock::now();
            auto sleep_time = 100ms;

            while (res.code == 202)
            {
                // Use a raw mxb::http:get(), the `self` already includes the hostname and port.
                res = mxb::http::get(self, "admin", "mariadb");
                response.reset();
                response.load_string(res.body);

                if (res.code == 202)
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
                }
            }

            if (res.code == 201)
            {
                response.at("data/attributes/results").try_get_bool("ok", &ok);
            }
            else
            {
                m_test.tprintf("ETL failed:\n%s", res.body.c_str());
                response.reset();
                break;
            }
        }

        del(mxb::cat("sql/", source_id, "?token=", source_token));
        del(mxb::cat("sql/", dest_id, "?token=", dest_token));

        return {ok, response};
    }

private:

    std::string url(std::string_view endpoint)
    {
        return mxb::cat("http://", m_test.maxscale->ip(), ":8989/v1/", endpoint);
    }

    TestConnections& m_test;
};
