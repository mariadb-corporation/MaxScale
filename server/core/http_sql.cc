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
#include "internal/listener.hh"
#include "internal/servermanager.hh"
#include "internal/service.hh"
#include "internal/sql_conn_manager.hh"

#include <maxbase/json.hh>
#include <maxscale/json_api.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/cn_strings.hh>

#include <errmsg.h>
#include <maxbase/format.hh>

using std::string;
using std::move;

namespace
{

// Cookies where the connection ID token is stored
const std::string CONN_ID_BODY = "conn_id_body";
const std::string CONN_ID_SIG = "conn_id_sig";

const std::string TOKEN_ISSUER = "mxs-query";

const std::string COLLECTION_NAME = "sql";

HttpResponse create_error(const std::string& err, int errcode = MHD_HTTP_FORBIDDEN)
{
    mxb_assert(!err.empty());
    return HttpResponse(errcode, mxs_json_error("%s", err.c_str()));
}

std::pair<int64_t, std::string> get_connection_id(const HttpRequest& request, const std::string& requested_id)
{
    bool ok = false;
    int64_t id = 0;
    std::string aud;
    std::string err;
    std::string token = request.get_option("token");
    std::string body = request.get_cookie(CONN_ID_BODY);
    std::string sig = request.get_cookie(CONN_ID_SIG);

    if (!token.empty())
    {
        std::tie(ok, aud) = mxs::jwt::get_audience(TOKEN_ISSUER, token);
    }
    else if (!body.empty() && !sig.empty())
    {
        std::tie(ok, aud) = mxs::jwt::get_audience(TOKEN_ISSUER, body + sig);
    }
    else if (!requested_id.empty())
    {
        err = "No token provided, expected a token for connection " + requested_id;
    }
    else
    {
        ok = true;
    }

    if (ok)
    {
        if (requested_id.empty() || aud == requested_id)
        {
            if (!aud.empty())
            {
                id = strtol(aud.c_str(), nullptr, 10);
            }
        }
        else
        {
            err = "URL is for connection " + requested_id + ", token is for connection " + aud;
        }
    }
    else
    {
        err = "Malformed connection token";
    }

    return {id, err};
}

int64_t get_page_size(const HttpRequest& request)
{
    int64_t page_size = 0;
    auto opt = request.get_option("page[size]");

    if (!opt.empty())
    {
        page_size = strtol(opt.c_str(), nullptr, 10);
    }

    return page_size;
}

json_t* generate_column_info(const mxq::MariaDBQueryResult::FieldInfo& field_info)
{
    json_t* rval = json_array();
    int n = field_info.n;
    for (int i = 0; i < n; i++)
    {
        json_array_append_new(rval, json_string(field_info.fields[i].name));
    }
    return rval;
}

json_t* generate_resultdata_row(mxq::MariaDBQueryResult* resultset,
                                const mxq::MariaDBQueryResult::FieldInfo& field_info)
{
    json_t* rval = json_array();
    auto n = field_info.n;
    auto fields = field_info.fields;
    auto rowdata = resultset->rowdata();

    for (int i = 0; i < n; i++)
    {
        json_t* value = nullptr;

        if (rowdata[i])
        {
            switch (fields[i].type)
            {
            case MYSQL_TYPE_DECIMAL:
            case MYSQL_TYPE_TINY:
            case MYSQL_TYPE_SHORT:
            case MYSQL_TYPE_LONG:
            case MYSQL_TYPE_LONGLONG:
            case MYSQL_TYPE_INT24:
                value = json_integer(strtol(rowdata[i], nullptr, 10));
                break;

            case MYSQL_TYPE_FLOAT:
            case MYSQL_TYPE_DOUBLE:
                value = json_real(strtod(rowdata[i], nullptr));
                break;

            case MYSQL_TYPE_NULL:
                value = json_null();
                break;

            default:
                value = json_string(rowdata[i]);
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

        json_array_append_new(rval, value);
    }
    return rval;
}

json_t* generate_json_representation(mxq::MariaDB& conn)
{
    using ResultType = mxq::MariaDB::ResultType;
    json_t* resultset_arr = json_array();

    auto current_type = conn.current_result_type();
    while (current_type != ResultType::NONE)
    {
        switch (current_type)
        {
        case ResultType::OK:
            {
                auto res = conn.get_ok_result();
                json_t* ok = json_object();
                json_object_set_new(ok, "last_insert_id", json_integer(res->insert_id));
                json_object_set_new(ok, "warnings", json_integer(res->warnings));
                json_object_set_new(ok, "affected_rows", json_integer(res->affected_rows));
                json_array_append_new(resultset_arr, ok);
            }
            break;

        case ResultType::ERROR:
            {
                auto res = conn.get_error_result();
                json_t* err = json_object();
                json_object_set_new(err, "errno", json_integer(res->error_num));
                json_object_set_new(err, "message", json_string(res->error_msg.c_str()));
                json_object_set_new(err, "sqlstate", json_string(res->sqlstate.c_str()));
                json_array_append_new(resultset_arr, err);
            }
            break;

        case ResultType::RESULTSET:
            {
                // A resultset may be sent in multiple parts if it doesn't fit in one page.
                // TODO: When continuing a previous resultset, do not add the header information. Also
                // add page support.
                auto res = conn.get_resultset();
                auto fields = res->field_info();
                json_t* resultset = json_object();
                json_object_set_new(resultset, "fields", generate_column_info(fields));
                json_t* rows = json_array();
                while (res->next_row())
                {
                    json_array_append_new(rows, generate_resultdata_row(res.get(), fields));
                }
                json_object_set_new(resultset, "data", rows);
                json_array_append_new(resultset_arr, resultset);
            }
            break;

        case ResultType::NONE:
            break;
        }
        current_type = conn.next_result();
    }

    return resultset_arr;
}

HttpResponse construct_result_response(json_t* resultdata, bool more_data, int64_t page_size,
                                       const string& host, const std::string& self,
                                       const string& query_id)
{
    json_t* obj = json_object();
    json_object_set_new(obj, CN_ID, json_string(query_id.c_str()));
    json_object_set_new(obj, CN_TYPE, json_string("queries"));
    json_t* attr = json_object();
    json_object_set_new(attr, "results", resultdata);
    json_object_set_new(obj, CN_ATTRIBUTES, attr);
    json_t* rval = mxs_json_resource(host.c_str(), self.c_str(), obj);

    // Create pagination links
    json_t* links = json_object_get(rval, CN_LINKS);
    string base = json_string_value(json_object_get(links, "self"));
    HttpResponse response(MHD_HTTP_OK, rval);

    if (more_data)
    {
        auto next = base + "?page[size]=" + std::to_string(page_size);
        json_object_set_new(links, "next", json_string(next.c_str()));
        response.add_header(MHD_HTTP_HEADER_LOCATION, base);
    }

    return response;
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

json_t* connection_json_data(const std::string& host, const std::string& id_str)
{
    json_t* data = json_object();
    json_t* self = mxs_json_self_link(host.c_str(), COLLECTION_NAME.c_str(), id_str.c_str());
    std::string self_link = json_string_value(json_object_get(self, "self"));
    std::string query_link = self_link + "queries/";
    json_object_set_new(self, "related", json_string(query_link.c_str()));

    json_object_set_new(data, CN_TYPE, json_string(COLLECTION_NAME.c_str()));
    json_object_set_new(data, CN_ID, json_string(id_str.c_str()));
    json_object_set_new(data, CN_LINKS, self);

    return data;
}

json_t* one_connection_to_json(const std::string& host, const std::string& id_str)
{
    std::string self = COLLECTION_NAME + "/" + id_str;
    return mxs_json_resource(host.c_str(), self.c_str(), connection_json_data(host, id_str));
}

json_t* all_connections_to_json(const std::string& host, const std::vector<int64_t>& connections)
{
    json_t* arr = json_array();

    for (auto id : connections)
    {
        json_array_append_new(arr, connection_json_data(host, std::to_string(id)));
    }

    return mxs_json_resource(host.c_str(), COLLECTION_NAME.c_str(), arr);
}

HttpResponse create_connect_response(const std::string& host, int64_t id, bool persist)
{
    // TODO: Figure out how long the connections should be kept valid
    int max_age = 28800;
    std::string id_str = std::to_string(id);
    auto token = mxs::jwt::create(TOKEN_ISSUER, id_str, max_age);

    json_t* data = one_connection_to_json(host, id_str);
    HttpResponse response(MHD_HTTP_CREATED, data);
    response.add_header(MHD_HTTP_HEADER_LOCATION, host + COLLECTION_NAME + "/" + id_str);

    if (persist)
    {
        response.add_split_cookie(CONN_ID_BODY, CONN_ID_SIG, token, max_age);
    }
    else
    {
        json_object_set_new(data, "meta", json_pack("{s:s}", "token", token.c_str()));
    }

    return response;
}



HttpSql::ConnectionManager manager;
}

//
// Public API functions
//

namespace HttpSql
{
HttpResponse connect(const HttpRequest& request)
{
    mxb::Json json(request.get_json());
    ConnectionConfig config;
    json.try_get_int("timeout", &config.timeout);
    json.try_get_string("db", &config.db);
    int64_t id;
    std::string err;
    std::tie(id, err) = get_connection_id(request, "");

    if (id)
    {
        close_connection(id);
    }
    else if (!err.empty())
    {
        return create_error(err);
    }

    std::string target;

    if (!json.try_get_string("user", &config.user)
        || !json.try_get_string("password", &config.password)
        || !json.try_get_string("target", &target))
    {
        return create_error("The `target`, `user` and `password` fields are mandatory");
    }

    if (Server* server = ServerManager::find_by_unique_name(target))
    {
        config.host = server->address();
        config.port = server->port();
        config.ssl = server->ssl_config();
    }
    else if (Service* service = Service::find(target))
    {
        auto listeners = listener_find_by_service(service);

        if (listeners.empty())
        {
            return create_error("Service '" + target + "' has no listeners");
        }
        else if (listeners.size() > 1)
        {
            return create_error("Service '" + target + "' has more than one listener,"
                                + " connect to a listener directly.");
        }

        auto listener = listeners.front();

        if (listener->type() == Listener::Type::UNIX_SOCKET)
        {
            return create_error("Listener for service '" + target + "' is configured with UNIX socket");
        }

        config.port = listener->port();
        config.host = listener->address();
        config.ssl = listener->ssl_config();
    }
    else if (auto listener = listener_find(target))
    {
        if (listener->type() == Listener::Type::UNIX_SOCKET)
        {
            return create_error("Listener '" + target + "' is configured with UNIX socket");
        }

        config.port = listener->port();
        config.host = listener->address();
        config.ssl = listener->ssl_config();
    }
    else
    {
        return create_error("Target '" + target + "' not found");
    }

    bool persist = request.get_option("persist") == "yes";
    std::string host = request.host();

    return HttpResponse(
        [config, persist, host]() {
            std::string err;

            if (int64_t new_id = create_connection(config, &err))
            {
                return create_connect_response(host, new_id, persist);
            }
            else
            {
                return HttpResponse(MHD_HTTP_FORBIDDEN, mxs_json_error("%s", err.c_str()));
            }
        });
}

// static
HttpResponse show_connection(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, one_connection_to_json(request.host(), request.uri_part(1)));
}

// static
HttpResponse show_all_connections(const HttpRequest& request)
{
    return HttpResponse(MHD_HTTP_OK, all_connections_to_json(request.host(), get_connections()));
}

// static
HttpResponse query(const HttpRequest& request)
{
    mxb::Json json(request.get_json());
    std::string sql;
    std::string err;
    int64_t id;
    std::tie(id, err) = get_connection_id(request, request.uri_part(1));

    if (!id)
    {
        return create_error(err);
    }
    else if (!json.try_get_string("sql", &sql))
    {
        return HttpResponse(MHD_HTTP_FORBIDDEN, mxs_json_error("No `sql` defined."));
    }

    std::string host = request.host();
    std::string self = request.get_uri();
    int64_t page_size = get_page_size(request);

    auto exec_query_cb = [id, sql, host, self, page_size]() {
            int64_t query_id = 0;
            json_t* result_data = nullptr;

            auto c = manager.get(id);
            if (c)
            {
                c->conn.streamed_query(sql);
                query_id = c->query_id++;
                result_data = generate_json_representation(c->conn);
                manager.put(id);

                string id_str = std::to_string(id) + "-" + std::to_string(query_id);
                string self_id = self + "/" + id_str;
                HttpResponse response = construct_result_response(result_data, false, page_size,
                                                                  host, self_id, id_str);
                response.set_code(MHD_HTTP_CREATED);

                // Add the request SQL into the initial response
                json_t* attr = mxs_json_pointer(response.get_response(), "/data/attributes");
                mxb_assert(attr);
                json_object_set_new(attr, "sql", json_string(sql.c_str()));

                return response;
            }
            else
            {
                string errmsg = mxb::string_printf("ID %li not found.", id);
                return create_error(errmsg, MHD_HTTP_SERVICE_UNAVAILABLE);
            }
        };
    return HttpResponse(std::move(exec_query_cb));
}

// static
HttpResponse result(const HttpRequest& request)
{
    mxb::Json json(request.get_json());
    std::string err;
    int64_t id;
    std::tie(id, err) = get_connection_id(request, request.uri_part(1));

    if (!id)
    {
        return create_error(err);
    }

    std::string host = request.host();
    std::string self = request.get_uri();
    std::string query_id = request.uri_part(request.uri_part_count() - 1);
    int64_t page_size = get_page_size(request);
    auto conn_data = manager.get(id);

    return HttpResponse(
        [conn_data, id, host, self, query_id, page_size]() {
            HttpResponse rval(MHD_HTTP_NOT_FOUND);
            return rval;
        });
}

// static
HttpResponse disconnect(const HttpRequest& request)
{
    std::string err;
    int64_t id;
    std::tie(id, err) = get_connection_id(request, request.uri_part(1));

    if (!id)
    {
        return create_error(err);
    }

    return HttpResponse(
        [id]() {
            close_connection(id);
            HttpResponse response(MHD_HTTP_NO_CONTENT);
            response.remove_split_cookie(CONN_ID_BODY, CONN_ID_SIG);
            return response;
        });
}

//
// SQL connection implementation
//

// static
bool is_query(const std::string& id)
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

// static
bool is_connection(const std::string& id)
{
    return manager.is_connection(strtol(id.c_str(), nullptr, 10));
}

// static
std::vector<int64_t> get_connections()
{
    return manager.get_connections();
}

// static
int64_t create_connection(const ConnectionConfig& config, std::string* err)
{
    int64_t id = 0;
    mxq::MariaDB conn;
    auto& sett = conn.connection_settings();
    sett.user = config.user;
    sett.password = config.password;
    sett.timeout = config.timeout;
    sett.ssl = config.ssl;

    if (conn.open(config.host, config.port, config.db))
    {
        id = manager.add(move(conn));
    }
    else
    {
        *err = conn.error();
    }

    return id;
}

// static
std::vector<std::unique_ptr<HttpSql::Result>>
read_result(int64_t id, int64_t rows_max, bool* more_results)
{
    std::vector<std::unique_ptr<HttpSql::Result>> results;
    auto c = manager.get(id);

    if (c->conn.is_open())
    {
        if (c->expecting_result)
        {
            /*
             *  results.push_back(format_result(c.conn));
             *
             *  while (mysql_more_results(c.conn))
             *  {
             *   mysql_next_result(c.conn);
             *   results.push_back(format_result(c.conn));
             *  }
             *
             *  c.expecting_result = false; */
        }

        manager.put(id);
    }

    return results;
}

void close_connection(int64_t id)
{
    manager.erase(id);
}
}
