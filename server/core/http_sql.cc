/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-12
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

#include <arpa/inet.h>

#include <maxbase/json.hh>
#include <maxscale/json_api.hh>
#include <maxscale/cn_strings.hh>

#include <maxbase/format.hh>

using std::string;
using std::move;

namespace
{

// Cookies where the connection ID token is stored
const std::string CONN_ID_BODY = "conn_id_body_";
const std::string CONN_ID_SIG = "conn_id_sig_";

const std::string TOKEN_ISSUER = "mxs-query";

const std::string COLLECTION_NAME = "sql";

HttpResponse create_error(const std::string& err, int errcode = MHD_HTTP_BAD_REQUEST)
{
    mxb_assert(!err.empty());
    return HttpResponse(errcode, mxs_json_error("%s", err.c_str()));
}

std::string get_conn_id_cookie(const HttpRequest& request, const std::string& id)
{
    return request.get_cookie(CONN_ID_SIG + id);
}

void set_conn_id_cookie(HttpResponse* response, const std::string& id,
                        const std::string& token, uint32_t max_age)
{
    response->add_cookie(CONN_ID_SIG + id, token, max_age);
}

std::pair<std::string, std::string> get_connection_id(const HttpRequest& request,
                                                      const std::string& requested_id)
{
    bool ok = false;
    std::string id;
    std::string aud;
    std::string err;
    std::string token = request.get_option("token");
    std::string cookie = get_conn_id_cookie(request, requested_id);

    if (!token.empty() || !cookie.empty())
    {
        if (auto claims = mxs::jwt::decode(TOKEN_ISSUER, token.empty() ? cookie : token))
        {
            if (auto sub = claims->get("sub"))
            {
                ok = true;
                aud = sub.value();
            }
        }
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
                id = aud;
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

json_t* generate_column_info(const mxq::MariaDBQueryResult::Fields& fields_info)
{
    json_t* rval = json_array();
    for (auto& elem : fields_info)
    {
        json_array_append_new(rval, json_string(elem.name.c_str()));
    }
    return rval;
}

json_t* generate_resultdata_row(mxq::MariaDBQueryResult* resultset,
                                const mxq::MariaDBQueryResult::Fields& field_info)
{
    using Type = mxq::MariaDBQueryResult::Field::Type;
    json_t* rval = json_array();
    auto n = field_info.size();
    auto rowdata = resultset->rowdata();

    for (size_t i = 0; i < n; i++)
    {
        json_t* value = nullptr;

        if (rowdata[i])
        {
            switch (field_info[i].type)
            {
            case Type::INTEGER:
                value = json_integer(strtol(rowdata[i], nullptr, 10));
                break;

            case Type::FLOAT:
                value = json_real(strtod(rowdata[i], nullptr));
                break;

            case Type::NUL:
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

json_t* generate_json_representation(mxq::MariaDB& conn, int64_t max_rows)
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
                auto res = conn.get_resultset();
                auto fields = res->fields();
                json_t* resultset = json_object();
                json_t* rows = json_array();
                int64_t rows_read = 0;

                // We have to read the whole resultset in order to find whether it ended with an error
                while (res->next_row())
                {
                    if (rows_read++ < max_rows)
                    {
                        json_array_append_new(rows, generate_resultdata_row(res.get(), fields));
                    }
                }

                auto error = conn.get_error_result();

                if (error->error_num)
                {
                    json_object_set_new(resultset, "errno", json_integer(error->error_num));
                    json_object_set_new(resultset, "message", json_string(error->error_msg.c_str()));
                    json_object_set_new(resultset, "sqlstate", json_string(error->sqlstate.c_str()));
                    json_decref(rows);
                }
                else
                {
                    json_object_set_new(resultset, "data", rows);
                    json_object_set_new(resultset, "fields", generate_column_info(fields));
                    json_object_set_new(resultset, "complete", json_boolean(rows_read < max_rows));
                }

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

HttpResponse
construct_result_response(json_t* resultdata, const string& host, const std::string& self,
                          const string& query_id, const mxb::Duration& query_exec_time)
{
    json_t* obj = json_object();
    json_object_set_new(obj, CN_ID, json_string(query_id.c_str()));
    json_object_set_new(obj, CN_TYPE, json_string("queries"));

    json_t* attr = json_object();
    json_object_set_new(attr, "results", resultdata);
    auto exec_time = mxb::to_secs(query_exec_time);
    json_object_set_new(attr, "execution_time", json_real(exec_time));
    json_object_set_new(obj, CN_ATTRIBUTES, attr);
    json_t* rval = mxs_json_resource(host.c_str(), self.c_str(), obj);

    HttpResponse response(MHD_HTTP_OK, rval);
    return response;
}

bool is_zero_address(const std::string& ip)
{
    if (ip.find(":") == std::string::npos)
    {
        // There's only one way to express the zero address for IPv4.
        return ip == "0.0.0.0";
    }
    else
    {
        // IPv6 has multiple ways of indicating the zero address. Converting it into the binary form allows
        // them all to be detected.
        in6_addr addr;
        decltype(addr.s6_addr) zero = {};
        return inet_pton(AF_INET6, ip.c_str(), &addr) == 1 && memcmp(addr.s6_addr, zero, sizeof(zero)) == 0;
    }
}
}

namespace
{

struct ThisUnit
{
    HttpSql::ConnectionManager manager;
};
ThisUnit this_unit;

json_t* connection_json_data(const std::string& host, const std::string& id)
{
    json_t* data = json_object();
    json_t* self = mxs_json_self_link(host.c_str(), COLLECTION_NAME.c_str(), id.c_str());
    std::string self_link = json_string_value(json_object_get(self, "self"));
    std::string query_link = self_link + "queries/";
    json_object_set_new(self, "related", json_string(query_link.c_str()));

    json_object_set_new(data, CN_TYPE, json_string(COLLECTION_NAME.c_str()));
    json_object_set_new(data, CN_ID, json_string(id.c_str()));
    json_object_set_new(data, CN_LINKS, self);
    json_object_set_new(data, CN_ATTRIBUTES, this_unit.manager.connection_to_json(id));

    return data;
}

json_t* one_connection_to_json(const std::string& host, const std::string& id)
{
    std::string self = COLLECTION_NAME + "/" + id;
    return mxs_json_resource(host.c_str(), self.c_str(), connection_json_data(host, id));
}

json_t* all_connections_to_json(const std::string& host, const std::vector<std::string>& connections)
{
    json_t* arr = json_array();

    for (auto id : connections)
    {
        json_array_append_new(arr, connection_json_data(host, id));
    }

    return mxs_json_resource(host.c_str(), COLLECTION_NAME.c_str(), arr);
}

HttpResponse create_connect_response(const std::string& host, const std::string& id, bool persist, int age)
{
    int max_age = age > 0 ? age : 28800;
    auto token = mxs::jwt::create(TOKEN_ISSUER, id, max_age);

    json_t* data = one_connection_to_json(host, id);
    HttpResponse response(MHD_HTTP_CREATED, data);
    response.add_header(MHD_HTTP_HEADER_LOCATION, host + COLLECTION_NAME + "/" + id);

    if (persist)
    {
        set_conn_id_cookie(&response, id, token, max_age);
    }
    else
    {
        json_object_set_new(data, "meta", json_pack("{s:s}", "token", token.c_str()));
    }

    return response;
}
}

//
// Public API functions
//

namespace HttpSql
{

void start_cleanup()
{
    this_unit.manager.start_cleanup_thread();
}

void stop_cleanup()
{
    this_unit.manager.stop_cleanup_thread();
}

HttpResponse connect(const HttpRequest& request)
{
    mxb::Json json(request.get_json());
    ConnectionConfig config;
    json.try_get_int("timeout", &config.timeout);
    json.try_get_string("db", &config.db);
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
        config.proxy_protocol = server->proxy_protocol();
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

    if (is_zero_address(config.host))
    {
        // By default listeners listen on the "zero address" that accepts connections from any network
        // interface. The address is only valid for the listening side and using it on the connecting side is
        // wrong.
        config.host = "127.0.0.1";
    }

    bool persist = request.get_option("persist") == "yes";
    int max_age = atoi(request.get_option("max-age").c_str());
    std::string host = request.host();

    return HttpResponse(
        [config, persist, host, max_age]() {
        std::string err;
        std::string new_id = create_connection(config, &err);
        if (!new_id.empty())
        {
            return create_connect_response(host, new_id, persist, max_age);
        }
        else
        {
            return HttpResponse(MHD_HTTP_BAD_REQUEST, mxs_json_error("%s", err.c_str()));
        }
    });
}

HttpResponse reconnect(const HttpRequest& request)
{
    string err;
    string id;
    std::tie(id, err) = get_connection_id(request, request.uri_part(1));

    if (id.empty())
    {
        return create_error(err);
    }

    auto cb = [id, host = string(request.host())]() {
        HttpResponse response;

        if (auto managed_conn = this_unit.manager.get_connection(id))
        {
            if (managed_conn->conn.reconnect())
            {
                response = HttpResponse(MHD_HTTP_NO_CONTENT);
            }
            else
            {
                response = create_error(managed_conn->conn.error(), MHD_HTTP_SERVICE_UNAVAILABLE);
            }

            managed_conn->release();
        }
        else
        {
            response = create_error(mxb::string_printf("ID %s not found or is busy.", id.c_str()),
                                    MHD_HTTP_SERVICE_UNAVAILABLE);
        }

        return response;
    };

    return HttpResponse(cb);
}

HttpResponse clone(const HttpRequest& request)
{
    string err;
    string id;
    std::tie(id, err) = get_connection_id(request, request.uri_part(1));

    if (id.empty())
    {
        return create_error(err);
    }

    bool persist = request.get_option("persist") == "yes";
    int max_age = atoi(request.get_option("max-age").c_str());

    auto cb = [id, host = string(request.host()), persist, max_age]() {
        HttpResponse response;

        if (auto config = this_unit.manager.get_configuration(id))
        {
            std::string err;
            std::string new_id = create_connection(config.value(), &err);
            if (!new_id.empty())
            {
                response = create_connect_response(host, new_id, persist, max_age);
            }
            else
            {
                response = HttpResponse(MHD_HTTP_BAD_REQUEST, mxs_json_error("%s", err.c_str()));
            }
        }
        else
        {
            response = create_error(mxb::string_printf("ID %s not found.", id.c_str()),
                                    MHD_HTTP_SERVICE_UNAVAILABLE);
        }

        return response;
    };

    return HttpResponse(cb);
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
    string sql;
    string err;
    string id;
    std::tie(id, err) = get_connection_id(request, request.uri_part(1));

    if (id.empty())
    {
        return create_error(err);
    }
    else if (!json.try_get_string("sql", &sql))
    {
        return HttpResponse(MHD_HTTP_BAD_REQUEST, mxs_json_error("No `sql` defined."));
    }

    // Optional row limit. 1000 is default. Can crash if client asks for more data than MaxScale has memory
    // for.
    int64_t max_rows = 1000;
    json.try_get_int("max_rows", &max_rows);
    if (max_rows < 0)
    {
        return HttpResponse(MHD_HTTP_BAD_REQUEST, mxs_json_error("`max_rows` cannot be negative."));
    }

    string host = request.host();
    string self = request.get_uri();

    auto exec_query_cb = [id, max_rows, sql = move(sql), host = move(host), self = move(self)]() {
        auto managed_conn = this_unit.manager.get_connection(id);
        if (managed_conn)
        {
            if (managed_conn->last_max_rows != max_rows)
            {
                std::string limit = max_rows ? std::to_string(max_rows) : "DEFAULT";
                managed_conn->conn.cmd("SET sql_select_limit=" + limit);
                managed_conn->last_max_rows = max_rows ? max_rows : std::numeric_limits<int64_t>::max();
            }

            int64_t query_id = ++managed_conn->current_query_id;
            auto time_before = mxb::Clock::now();
            managed_conn->conn.streamed_query(sql);
            auto time_after = mxb::Clock::now();
            auto exec_time = time_after - time_before;
            managed_conn->last_query_time = time_after;

            json_t* result_data = generate_json_representation(managed_conn->conn,
                                                               managed_conn->last_max_rows);
            managed_conn->release();
            // 'managed_conn' is now effectively back in storage and should not be used.

            string id_str = mxb::string_printf("%s.%li", id.c_str(), query_id);
            string self_id = self;
            self_id.append("/").append(id_str);
            HttpResponse response = construct_result_response(result_data,
                                                              host, self_id, id_str, exec_time);
            response.set_code(MHD_HTTP_CREATED);

            // Add the request SQL into the initial response
            json_t* attr = mxb::json_ptr(response.get_response(), "/data/attributes");
            mxb_assert(attr);
            json_object_set_new(attr, "sql", json_string(sql.c_str()));

            return response;
        }
        else
        {
            string errmsg = mxb::string_printf("ID %s not found or is busy.", id.c_str());
            return create_error(errmsg, MHD_HTTP_SERVICE_UNAVAILABLE);
        }
    };
    return HttpResponse(std::move(exec_query_cb));
}

// static
HttpResponse disconnect(const HttpRequest& request)
{
    std::string err;
    std::string id;
    std::tie(id, err) = get_connection_id(request, request.uri_part(1));

    if (id.empty())
    {
        return create_error(err);
    }

    return HttpResponse(
        [id]() {
        if (this_unit.manager.erase(id))
        {
            HttpResponse response(MHD_HTTP_NO_CONTENT);
            response.remove_cookie(CONN_ID_SIG + id);
            return response;
        }
        else
        {
            string error_msg = mxb::string_printf("Connection %s not found or is busy.", id.c_str());
            return create_error(error_msg, MHD_HTTP_NOT_FOUND);
        }
    });
}

//
// SQL connection implementation
//

// static
bool is_query(const std::string& id)
{
    bool rval = false;
    auto pos = id.find('.');

    if (pos != std::string::npos)
    {
        std::string conn_id = id.substr(0, pos);
        int64_t query_id = strtol(id.substr(pos + 1).c_str(), nullptr, 10);
        rval = this_unit.manager.is_query(conn_id, query_id);
    }

    return rval;
}

// static
bool is_connection(const std::string& id)
{
    return this_unit.manager.is_connection(id);
}

// static
std::vector<std::string> get_connections()
{
    return this_unit.manager.get_connections();
}

// static
std::string create_connection(const ConnectionConfig& config, std::string* err)
{
    std::string id;
    mxq::MariaDB conn;
    auto& sett = conn.connection_settings();
    sett.user = config.user;
    sett.password = config.password;
    sett.timeout = config.timeout;
    sett.ssl = config.ssl;
    sett.proxy_protocol = config.proxy_protocol;
    sett.local_infile = false;

    if (conn.open(config.host, config.port, config.db))
    {
        id = this_unit.manager.add(move(conn), config);
    }
    else
    {
        *err = conn.error();
    }

    return id;
}
}
