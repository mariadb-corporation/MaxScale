/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/http_sql.hh"
#include "internal/jwt.hh"
#include "internal/servermanager.hh"

#include <maxbase/json.hh>
#include <maxscale/json_api.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/cn_strings.hh>

namespace
{

// Cookies where the connection ID token is stored
const std::string CONN_ID_BODY = "conn_id_body";
const std::string CONN_ID_SIG = "conn_id_sig";

const std::string TOKEN_ISSUER = "mxs-query";

const HttpResponse no_id_error(
    MHD_HTTP_FORBIDDEN,
    mxs_json_error("No connection token in cookies or in `connection_id`."));

int64_t get_connection_id(const HttpRequest& request)
{
    bool ok = false;
    std::string aud;
    int64_t id = 0;
    std::string token = request.get_option("token");

    if (!token.empty())
    {
        std::tie(ok, aud) = mxs::jwt::get_audience(TOKEN_ISSUER, token);
    }
    else
    {
        auto body = request.get_cookie(CONN_ID_BODY);
        auto sig = request.get_cookie(CONN_ID_SIG);

        if (!body.empty() && !sig.empty())
        {
            std::tie(ok, aud) = mxs::jwt::get_audience(TOKEN_ISSUER, body + sig);
        }
    }

    if (ok)
    {
        id = strtol(aud.c_str(), nullptr, 10);
    }

    return id;
}
}

class RowsResult : public HttpSql::Result
{
public:
    RowsResult(MYSQL* conn, MYSQL_RES* res)
    {
        int64_t rows = 0;
        json_t* data = json_array();
        json_t* meta = json_array();
        int n = mysql_field_count(conn);
        MYSQL_FIELD* fields = mysql_fetch_fields(res);

        for (int i = 0; i < n; i++)
        {
            json_array_append_new(meta, json_string(fields[i].name));
        }

        while (auto row = mysql_fetch_row(res))
        {
            json_t* arr = json_array();

            for (int i = 0; i < n; i++)
            {
                json_t* value = nullptr;

                if (row[i])
                {
                    switch (fields[i].type)
                    {
                    case MYSQL_TYPE_DECIMAL:
                    case MYSQL_TYPE_TINY:
                    case MYSQL_TYPE_SHORT:
                    case MYSQL_TYPE_LONG:
                    case MYSQL_TYPE_LONGLONG:
                    case MYSQL_TYPE_INT24:
                        value = json_integer(strtol(row[i], nullptr, 10));
                        break;

                    case MYSQL_TYPE_FLOAT:
                    case MYSQL_TYPE_DOUBLE:
                        value = json_real(strtod(row[i], nullptr));
                        break;

                    case MYSQL_TYPE_NULL:
                        value = json_null();
                        break;

                    default:
                        value = json_string(row[i]);
                        break;
                    }

                    if (!value)
                    {
                        value = json_null();
                    }
                }
                else
                {
                    value = json_null();
                }

                json_array_append_new(arr, value);
            }

            mxb_assert(json_array_size(arr) == (size_t)n);
            json_array_append_new(data, arr);
        }

        m_json = json_object();
        json_object_set_new(m_json, "data", data);
        json_object_set_new(m_json, "fields", meta);
    }

    ~RowsResult()
    {
        json_decref(m_json);
    }

    json_t* to_json() const
    {
        return json_incref(m_json);
    }

private:
    json_t* m_json;
};

class OkResult : public HttpSql::Result
{
public:
    OkResult(MYSQL* conn)
        : m_insert_id(mysql_insert_id(conn))
        , m_warnings(mysql_warning_count(conn))
        , m_affected_rows(mysql_affected_rows(conn))
    {
    }

    json_t* to_json() const
    {
        json_t* rval = json_object();
        json_object_set_new(rval, "last_insert_id", json_integer(m_insert_id));
        json_object_set_new(rval, "warnings", json_integer(m_warnings));
        json_object_set_new(rval, "affected_rows", json_integer(m_affected_rows));
        return rval;
    }

private:
    int m_insert_id;
    int m_warnings;
    int m_affected_rows;
};

class ErrResult : public HttpSql::Result
{
public:
    ErrResult(MYSQL* conn)
        : m_errno(mysql_errno(conn))
        , m_errmsg(mysql_error(conn))
        , m_sqlstate(mysql_sqlstate(conn))
    {
    }

    json_t* to_json() const
    {
        json_t* rval = json_object();
        json_object_set_new(rval, "errno", json_integer(m_errno));
        json_object_set_new(rval, "message", json_string(m_errmsg.c_str()));
        json_object_set_new(rval, "sqlstate", json_string(m_sqlstate.c_str()));
        return rval;
    }

private:
    int         m_errno;
    std::string m_errmsg;
    std::string m_sqlstate;
};

namespace
{

// TODO: Use a std::unique_ptr<QueryResult> as the source of the data
std::unique_ptr<HttpSql::Result> format_result(MYSQL* conn)
{
    std::unique_ptr<HttpSql::Result> rval;

    if (mysql_errno(conn))
    {
        rval = std::make_unique<ErrResult>(conn);
    }
    else if (auto res = mysql_use_result(conn))
    {
        rval = std::make_unique<RowsResult>(conn, res);
        mysql_free_result(res);
    }
    else
    {
        rval = std::make_unique<OkResult>(conn);
    }

    return rval;
}

HttpResponse create_connect_response(int64_t id, bool persist)
{
    // TODO: Figure out how long the connections should be kept valid
    int max_age = 28800;
    auto token = mxs::jwt::create(TOKEN_ISSUER, std::to_string(id), max_age);

    if (persist)
    {
        HttpResponse response(MHD_HTTP_NO_CONTENT);
        response.add_split_cookie(CONN_ID_BODY, CONN_ID_SIG, token, max_age);
        return response;
    }
    else
    {
        return HttpResponse(MHD_HTTP_OK, json_pack("{s:{s:s}}", "meta", "token", token.c_str()));
    }
}

// An extremely simple implementation of connection management for testing purposes.
class Manager
{
public:

    struct Connection
    {
        MYSQL*  conn {nullptr};
        bool    expecting_result {false};
        int64_t query_id {0};
    };

    Connection get(int64_t id)
    {
        Connection conn;
        std::lock_guard<std::mutex> guard(m_connection_lock);
        auto it = m_connections.find(id);

        if (it != m_connections.end())
        {
            conn = it->second;
            m_connections.erase(it);
        }

        return conn;
    }

    int64_t add(MYSQL* conn)
    {
        std::lock_guard<std::mutex> guard(m_connection_lock);
        int64_t id = m_id_gen++;
        Connection c {conn, false};
        m_connections.emplace(id, c);

        if (m_id_gen <= 0)
        {
            m_id_gen = 1;
        }

        return id;
    }

    void put(int64_t id, Connection conn)
    {
        std::lock_guard<std::mutex> guard(m_connection_lock);
        m_connections.emplace(id, conn);
    }

    bool is_query(int64_t conn_id, int64_t query_id) const
    {
        bool rval = false;
        std::lock_guard<std::mutex> guard(m_connection_lock);
        auto it = m_connections.find(conn_id);

        if (it != m_connections.end())
        {
            rval = query_id == it->second.query_id;
        }

        return rval;
    }

private:
    std::map<int64_t, Connection> m_connections;
    mutable std::mutex            m_connection_lock;
    int64_t                       m_id_gen {1};
};

Manager manager;
}

//
// Public API functions
//

// static
HttpResponse HttpSql::connect(const HttpRequest& request)
{
    mxb::Json json(request.get_json());
    ConnectionConfig config;
    json.try_get_int("timeout", &config.timeout);
    json.try_get_string("db", &config.db);

    if (int64_t id = get_connection_id(request))
    {
        close_connection(id);
    }

    if (!json.try_get_string("user", &config.user) || !json.try_get_string("password", &config.password))
    {
        return HttpResponse(MHD_HTTP_FORBIDDEN, mxs_json_error("Missing `user` or `password`."));
    }

    Server* server = ServerManager::find_by_unique_name(request.uri_part(1));
    mxb_assert(server);

    config.host = server->address();
    config.port = server->port();
    config.ssl = server->ssl_config();

    bool persist = request.get_option("persist") == "yes";

    return HttpResponse(
        [config, persist]() {
            std::string err;

            if (int64_t new_id = create_connection(config, &err))
            {
                return create_connect_response(new_id, persist);
            }
            else
            {
                return HttpResponse(MHD_HTTP_FORBIDDEN, mxs_json_error("%s", err.c_str()));
            }
        });
}

// static
HttpResponse HttpSql::query(const HttpRequest& request)
{
    mxb_assert(request.uri_part_count() == 3);
    mxb_assert(request.uri_part(0) == "servers");
    mxb_assert(request.uri_part(2) == "query");

    mxb::Json json(request.get_json());
    std::string sql;
    int64_t id = get_connection_id(request);

    if (!json.try_get_string("sql", &sql))
    {
        return HttpResponse(MHD_HTTP_FORBIDDEN, mxs_json_error("No `sql` defined."));
    }
    else if (!id)
    {
        return no_id_error;
    }

    std::string location_base = request.host() + request.get_uri();

    return HttpResponse(
        [id, sql, location_base]() {
            if (int64_t query_id = execute_query(id, sql))
            {
                HttpResponse response(MHD_HTTP_CREATED);
                auto location = location_base + "/" + std::to_string(id) + "-" + std::to_string(query_id);
                response.add_header(MHD_HTTP_HEADER_LOCATION, location);
                return response;
            }
            else
            {
                return HttpResponse(MHD_HTTP_FORBIDDEN, mxs_json_error("ID %ld not found", id));
            }
        });
}

// static
HttpResponse HttpSql::result(const HttpRequest& request)
{
    mxb::Json json(request.get_json());
    std::string sql;
    int64_t id = get_connection_id(request);

    if (!id)
    {
        return no_id_error;
    }

    std::string host = request.host();
    std::string self = request.get_uri();
    std::string query_id = request.uri_part(request.uri_part_count() - 1);

    int64_t page_size = 0;
    auto opt = request.get_option("page[size]");

    if (!opt.empty())
    {
        page_size = strtol(opt.c_str(), nullptr, 10);
    }

    return HttpResponse(
        [id, sql, host, self, page_size, query_id]() {
            bool more_results = false;
            std::vector<std::unique_ptr<Result>> results = read_result(id, page_size, &more_results);

            if (!results.empty())
            {
                mxb::Json js(mxb::Json::Type::OBJECT);

                json_t* arr = json_array();

                for (const auto& r : results)
                {
                    json_array_append_new(arr, r->to_json());
                }

                json_t* attr = json_object();
                json_object_set_new(attr, "results", arr);

                json_t* obj = json_object();
                json_object_set_new(obj, CN_ID, json_string(query_id.c_str()));
                json_object_set_new(obj, CN_TYPE, json_string("results"));
                json_object_set_new(obj, CN_ATTRIBUTES, attr);

                json_t* rval = mxs_json_resource(host.c_str(), self.c_str(), obj);

                // Create pagination links
                json_t* links = json_object_get(rval, CN_LINKS);
                std::string base = json_string_value(json_object_get(links, "self"));

                if (more_results)
                {
                    mxb_assert_message(page_size, "page_size must be defined if result is paginated");
                    auto next = base + "?page[size]=" + std::to_string(page_size);
                    json_object_set_new(links, "next", json_string(next.c_str()));
                }

                return HttpResponse(MHD_HTTP_OK, rval);
            }
            else
            {
                return HttpResponse(MHD_HTTP_NOT_FOUND);
            }
        });
}

// static
HttpResponse HttpSql::disconnect(const HttpRequest& request)
{
    int64_t id = get_connection_id(request);

    if (!id)
    {
        return no_id_error;
    }

    return HttpResponse(
        [id]() {
            close_connection(id);
            return HttpResponse(MHD_HTTP_NO_CONTENT);
        });
}

// static
bool HttpSql::is_query(const std::string& id)
{
    bool rval = false;
    auto pos = id.find('-');

    if (pos != std::string::npos)
    {
        int64_t conn_id = strtol(id.substr(0, pos).c_str(), nullptr, 10);
        int64_t query_id = strtol(id.substr(pos + 1).c_str(), nullptr, 10);
        rval = manager.is_query(conn_id, query_id);
    }

    return rval;
}

//
// SQL connection implementation
//

// static
int64_t HttpSql::create_connection(const ConnectionConfig& config, std::string* err)
{
    int64_t id = 0;
    MYSQL* conn = mysql_init(nullptr);

    unsigned int timeout = config.timeout;
    mysql_optionsv(conn, MYSQL_OPT_READ_TIMEOUT, &timeout);
    mysql_optionsv(conn, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
    mysql_optionsv(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    if (mxs_mysql_real_connect(conn, config.host.c_str(), config.port, config.user.c_str(),
                               config.password.c_str(), config.ssl,
                               CLIENT_MULTI_RESULTS | CLIENT_MULTI_STATEMENTS))
    {
        if (config.db.empty() || mysql_query(conn, ("USE `" + config.db + "`").c_str()) == 0)
        {
            id = manager.add(conn);
        }
        else
        {
            *err = mysql_error(conn);
            mysql_close(conn);
        }
    }
    else
    {
        *err = mysql_error(conn);
        mysql_close(conn);
    }

    return id;
}

// static
int64_t HttpSql::execute_query(int64_t id, const std::string& sql)
{
    int64_t query_id = 0;
    auto c = manager.get(id);

    if (c.conn)
    {
        mysql_real_query(c.conn, sql.c_str(), sql.size());
        c.expecting_result = true;
        ++c.query_id;

        if (c.query_id <= 0)
        {
            c.query_id = 1;
        }

        query_id = c.query_id;
        manager.put(id, c);
    }

    return query_id;
}

// static
std::vector<std::unique_ptr<HttpSql::Result>>
HttpSql::read_result(int64_t id, int64_t rows_max, bool* more_results)
{
    std::vector<std::unique_ptr<HttpSql::Result>> results;
    auto c = manager.get(id);

    if (c.conn)
    {
        if (c.expecting_result)
        {
            results.push_back(format_result(c.conn));

            while (mysql_more_results(c.conn))
            {
                mysql_next_result(c.conn);
                results.push_back(format_result(c.conn));
            }

            c.expecting_result = false;
        }

        manager.put(id, c);
    }

    return results;
}

// static
void HttpSql::close_connection(int64_t id)
{
    auto c = manager.get(id);
    mysql_close(c.conn);
}
