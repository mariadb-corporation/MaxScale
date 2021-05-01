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
    mxb::Json json(request.get_json());

    if (json.contains("connection_id"))
    {
        std::tie(ok, aud) = mxs::jwt::get_audience(TOKEN_ISSUER, json.get_string("conn_id"));
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

    return HttpResponse(
        [id, sql]() {
            execute_query(id, sql);
            // TODO: Return the result as JSON in the response
            read_result(id, 0);
            return HttpResponse(MHD_HTTP_NO_CONTENT);
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

//
// SQL connection implementation
//

// static
int64_t HttpSql::create_connection(const ConnectionConfig& config, std::string* err)
{
    // TODO: Create the connection
    return 1;
}

// static
bool HttpSql::execute_query(int64_t id, const std::string& sql)
{
    // TODO: Execute the query
    return true;
}

// static
std::vector<std::unique_ptr<HttpSql::Result>> HttpSql::read_result(int64_t id, int64_t rows_max)
{
    // TODO: Read the result
    return {};
}

// static
void HttpSql::close_connection(int64_t id)
{
    // TODO: Close the connection
}
