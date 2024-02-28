/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/http_sql.hh"

#include <arpa/inet.h>

#include <maxbase/format.hh>
#include <maxbase/json.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/json_api.hh>
#include <maxscale/listener.hh>
#include <maxscale/threadpool.hh>

#include "internal/jwt.hh"
#include "internal/servermanager.hh"
#include "internal/service.hh"
#include "internal/sql_conn_manager.hh"
#include "internal/sql_etl.hh"


using std::string;
using std::move;

namespace
{

// Cookies where the connection ID token is stored
const std::string CONN_ID_BODY = "conn_id_body_";
const std::string CONN_ID_SIG = "conn_id_sig_";

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

std::pair<std::string, std::string> validate_connection_id(const HttpRequest& request,
                                                           const std::string& requested_id,
                                                           const std::string& token)
{
    bool ok = false;
    std::string id;
    std::string aud;
    std::string err;

    if (!token.empty())
    {
        if (auto claims = mxs::jwt::decode(mxs::Config::get().admin_jwt_issuer, token))
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

std::pair<std::string, std::string> get_connection_id(const HttpRequest& request,
                                                      const std::string& requested_id)
{
    std::string token = request.get_option("token");
    std::string cookie = get_conn_id_cookie(request, requested_id);
    return validate_connection_id(request, requested_id, !token.empty() ? token : cookie);
}

HttpResponse
construct_result_response(json_t* resultdata, const string& host, const std::string& self,
                          const string& sql, const string& query_id, const mxb::Duration& query_exec_time)
{
    json_t* obj = json_object();
    json_object_set_new(obj, CN_ID, json_string(query_id.c_str()));
    json_object_set_new(obj, CN_TYPE, json_string("queries"));

    json_t* attr = json_object();

    if (resultdata)
    {
        json_object_set_new(attr, "results", resultdata);
    }

    json_object_set_new(attr, "execution_time", json_real(mxb::to_secs(query_exec_time)));
    json_object_set_new(attr, "sql", json_string(sql.c_str()));

    json_object_set_new(obj, CN_ATTRIBUTES, attr);
    json_t* rval = mxs_json_resource(host.c_str(), self.c_str(), obj);

    return HttpResponse(MHD_HTTP_CREATED, rval);
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

using Reason = HttpSql::ConnectionManager::Reason;

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
    int64_t max_age = age > 0 ? age : 28800;
    const auto& cnf = mxs::Config::get();
    max_age = std::min(max_age, cnf.admin_jwt_max_age.count());
    auto token = mxs::jwt::create(cnf.admin_jwt_issuer, id, max_age);

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

void init()
{
    this_unit.manager.start_cleanup_thread();
}

void finish()
{
    this_unit.manager.cancel_all_connections();
    this_unit.manager.stop_cleanup_thread();
}

HttpResponse connect(const HttpRequest& request)
{
    mxb::Json json(request.get_json());
    ConnectionConfig config;
    json.try_get_int("timeout", &config.timeout);
    json.try_get_string("db", &config.db);
    std::string target;

    if (!json.try_get_string("target", &target))
    {
        return create_error("The `target` field is mandatory");
    }

    if (target == "odbc")
    {
        if (!json.try_get_string("connection_string", &config.odbc_string) || config.odbc_string.empty())
        {
            return create_error("Missing connection string for ODBC connection.");
        }
    }
    else if (!json.try_get_string("user", &config.user) || !json.try_get_string("password", &config.password))
    {
        return create_error("The `user` and `password` fields are mandatory for this `target`");
    }

    config.target = target;

    if (!config.odbc_string.empty())
    {
        mxb_assert(target == "odbc");
        // ODBC connection. Everything we need to connect is defined in the connection string.
    }
    else if (Server* server = ServerManager::find_by_unique_name(target))
    {
        config.host = server->address();
        config.port = server->port();
        config.ssl = server->ssl_config();
        config.proxy_protocol = server->proxy_protocol();
    }
    else if (Service* service = Service::find(target))
    {
        auto listeners = mxs::Listener::find_by_service(service);

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

        if (listener->type() == mxs::Listener::Type::UNIX_SOCKET)
        {
            return create_error("Listener for service '" + target + "' is configured with UNIX socket");
        }

        config.port = listener->port();
        config.host = listener->address();
        config.ssl = listener->ssl_config();
    }
    else if (auto listener = mxs::Listener::find(target))
    {
        if (listener->type() == mxs::Listener::Type::UNIX_SOCKET)
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

    bool persist = request.is_truthy_option("persist");
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

        if (auto [managed_conn, reason, _] = this_unit.manager.get_connection(id); managed_conn)
        {
            if (managed_conn->reconnect())
            {
                response = HttpResponse(MHD_HTTP_NO_CONTENT);
            }
            else
            {
                response = create_error(managed_conn->error(), MHD_HTTP_SERVICE_UNAVAILABLE);
            }

            managed_conn->release();
        }
        else
        {
            const char* why = reason == Reason::BUSY ? "is busy" : "was not found";
            string errmsg = mxb::string_printf("ID %s %s.", id.c_str(), why);
            response = create_error(errmsg, MHD_HTTP_SERVICE_UNAVAILABLE);
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

    bool persist = request.is_truthy_option("persist");
    int max_age = atoi(request.get_option("max-age").c_str());

    auto cb = [id, host = string(request.host()), persist, max_age]() {
        HttpResponse response;

        if (auto config = this_unit.manager.get_configuration(id))
        {
            std::string errmsg;
            std::string new_id = create_connection(config.value(), &errmsg);
            if (!new_id.empty())
            {
                response = create_connect_response(host, new_id, persist, max_age);
            }
            else
            {
                response = HttpResponse(MHD_HTTP_BAD_REQUEST, mxs_json_error("%s", errmsg.c_str()));
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

    int64_t timeout = json.get_int("timeout");

    if (timeout < 0)
    {
        return HttpResponse(MHD_HTTP_BAD_REQUEST, mxs_json_error("`timeout` cannot be negative."));
    }

    bool async = request.is_truthy_option("async");
    string host = request.host();
    string self = request.get_uri();

    auto exec_query_cb = [id, max_rows, sql = move(sql), host = move(host), self = move(self),
                          async, timeout]() {
        if (auto [managed_conn, reason, _] = this_unit.manager.get_connection(id); managed_conn)
        {
            // Ignore any results that have not yet been read
            managed_conn->result.reset();

            int64_t query_id = ++managed_conn->current_query_id;
            string id_str = mxb::string_printf("%s.%li", id.c_str(), query_id);
            mxb::Json result_data(mxb::Json::Type::UNDEFINED);
            mxb::Duration exec_time {0};

            if (async)
            {
                mxs::thread_pool().execute(
                    [managed_conn = managed_conn, sql, max_rows, timeout]() {
                    managed_conn->result = managed_conn->query(sql, max_rows, timeout);
                    managed_conn->release();
                }, "sql" + id_str);
            }
            else
            {
                result_data = managed_conn->query(sql, max_rows, timeout);
                const auto& info = managed_conn->info();
                exec_time = info.last_query_ended - info.last_query_started;
                managed_conn->release();
                // 'managed_conn' is now effectively back in storage and should not be used.
            }

            string self_id = self;
            self_id.append("/").append(id_str);

            HttpResponse response = construct_result_response(result_data.release(), host, self_id,
                                                              sql, id_str, exec_time);

            if (async)
            {
                response.set_code(MHD_HTTP_ACCEPTED);
                response.add_header(MHD_HTTP_HEADER_LOCATION, host + "/" + self_id);
            }

            return response;
        }
        else
        {
            const char* why = reason == Reason::BUSY ? "is busy" : "was not found";
            string errmsg = mxb::string_printf("ID %s %s.", id.c_str(), why);
            return create_error(errmsg, MHD_HTTP_SERVICE_UNAVAILABLE);
        }
    };

    return HttpResponse(std::move(exec_query_cb));
}

HttpResponse query_result(const HttpRequest& request)
{
    auto [id, err] = get_connection_id(request, request.uri_part(1));

    if (id.empty())
    {
        return create_error(err);
    }

    std::string host = request.host();
    std::string self = request.get_uri();
    std::string query_id = request.uri_part(3);

    auto result_cb =
        [id = id, query_id = std::move(query_id), host = std::move(host), self = std::move(self)]() {
        HttpResponse response;

        if (auto [conn, reason, info] = this_unit.manager.get_connection(id); conn)
        {
            if (json_t* result_data = conn->result.get_json())
            {
                auto exec_time = info.last_query_ended - info.last_query_started;
                response = construct_result_response(json_incref(result_data),
                                                     host, self, info.sql, query_id, exec_time);
            }
            else
            {
                response = create_error("No async query results found.", MHD_HTTP_BAD_REQUEST);
            }

            conn->release();
        }
        else if (reason == Reason::BUSY)
        {
            auto exec_time = mxb::Clock::now() - info.last_query_started;
            response = construct_result_response(info.status.release(), host, self,
                                                 info.sql, query_id, exec_time);
            response.set_code(MHD_HTTP_ACCEPTED);
            response.add_header(MHD_HTTP_HEADER_LOCATION, host + "/" + self);
        }
        else
        {
            response = create_error(mxb::string_printf("ID %s was not found.", id.c_str()),
                                    MHD_HTTP_SERVICE_UNAVAILABLE);
        }

        return response;
    };

    return HttpResponse(std::move(result_cb));
}

HttpResponse erase_query_result(const HttpRequest& request)
{
    auto [id, err] = get_connection_id(request, request.uri_part(1));

    if (id.empty())
    {
        return create_error(err);
    }

    std::string host = request.host();
    std::string self = request.get_uri();

    auto result_cb = [id = id, host = std::move(host), self = std::move(self)]() {
        HttpResponse response;

        if (auto [conn, reason, _] = this_unit.manager.get_connection(id); conn)
        {
            conn->result.reset();
            conn->release();
        }
        else
        {
            const char* why = reason == Reason::BUSY ? "is busy" : "was not found";
            string errmsg = mxb::string_printf("ID %s %s.", id.c_str(), why);
            response = create_error(errmsg, MHD_HTTP_SERVICE_UNAVAILABLE);
        }

        return response;
    };

    return HttpResponse(std::move(result_cb));
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

HttpResponse cancel(const HttpRequest& request)
{
    auto [id, err] = get_connection_id(request, request.uri_part(1));

    if (id.empty())
    {
        return create_error(err);
    }

    return HttpResponse(
        [id = id]() {
        HttpResponse response;

        if (!this_unit.manager.cancel(id))
        {
            string errmsg = mxb::string_printf("ID %s was not found.", id.c_str());
            response = create_error(errmsg, MHD_HTTP_SERVICE_UNAVAILABLE);
        }

        return response;
    });
}

template<auto func>
HttpResponse run_etl_task(const HttpRequest& request)
{
    auto [id, err] = get_connection_id(request, request.uri_part(1));

    if (id.empty())
    {
        return create_error(err);
    }

    auto json = mxb::Json(request.get_json());
    std::string target;

    if (!json.try_get_string("target", &target))
    {
        return create_error("The `target` field is mandatory");
    }

    ConnectionConfig src_cc;
    ConnectionConfig dest_cc;

    if (auto cc = this_unit.manager.get_configuration(id))
    {
        src_cc = *cc;
    }
    else
    {
        mxb_assert_message(!true, "The connection should've been validated before the call.");
        return HttpResponse(MHD_HTTP_INTERNAL_SERVER_ERROR);
    }

    if (auto cnf = this_unit.manager.get_configuration(target))
    {
        std::string token = request.get_option("target_token");
        std::string cookie = get_conn_id_cookie(request, target);

        auto [target_id, target_err] = validate_connection_id(
            request, target, !token.empty() ? token : cookie);

        if (target_id.empty())
        {
            return create_error("Validation of target connection failed: " + target_err);
        }

        if (!ServerManager::find_by_unique_name(cnf->target))
        {
            return create_error("The target '" + cnf->target + "' of connection '"
                                + target + "' is not a server in MaxScale.");
        }

        dest_cc = *cnf;
    }
    else
    {
        return create_error("The target '" + target + "' is not a valid connection ID.");
    }

    std::shared_ptr<sql_etl::ETL> etl;

    try
    {
        etl = sql_etl::create(id, json, src_cc, dest_cc);
    }
    catch (const sql_etl::Error& e)
    {
        return create_error(e.what());
    }

    string host = request.host();
    string self = request.uri_segment(0, 2);

    auto exec_query_cb = [id = id, host = move(host), self = move(self), etl]() {
        if (auto [conn, reason, _] = this_unit.manager.get_connection(id); conn)
        {
            conn->set_cancel_handler([etl = etl](){
                etl->cancel();
            });

            conn->set_status_handler([etl](){
                return etl->to_json();
            });

            // Ignore any results that have not yet been read
            conn->result.reset();

            int64_t query_id = ++conn->current_query_id;
            string id_str = mxb::string_printf("%s.%li", id.c_str(), query_id);
            conn->query_start("ETL");

            mxs::thread_pool().execute([conn = conn, id, etl = move(etl)]() {
                conn->result = std::invoke(func, *etl);
                conn->query_end();
                conn->clear_status_handler();
                conn->clear_cancel_handler();
                conn->release();
            }, "etl-" + id_str);

            string self_id = self + "/queries/" + id_str;
            mxb::Duration exec_time {0};
            HttpResponse response = construct_result_response(nullptr, host, self_id,
                                                              "ETL", id_str, exec_time);

            response.set_code(MHD_HTTP_ACCEPTED);
            response.add_header(MHD_HTTP_HEADER_LOCATION, host + "/" + self_id);

            return response;
        }
        else
        {
            const char* why = reason == Reason::BUSY ? "is busy" : "was not found";
            string errmsg = mxb::string_printf("ID %s %s.", id.c_str(), why);
            return create_error(errmsg, MHD_HTTP_SERVICE_UNAVAILABLE);
        }
    };

    return HttpResponse(std::move(exec_query_cb));
}

HttpResponse odbc_drivers(const HttpRequest& request)
{
    json_t* arr = json_array();

    for (const auto& [driver, params] : mxq::ODBC::drivers())
    {
        json_t* obj = json_object();
        json_object_set_new(obj, CN_ID, json_string(driver.c_str()));
        json_object_set_new(obj, CN_TYPE, json_string("drivers"));


        json_t* attr = json_object();

        for (const auto& [k, v] : params)
        {
            json_object_set_new(attr, mxb::lower_case_copy(k).c_str(), json_string(v.c_str()));
        }

        json_object_set_new(obj, CN_ATTRIBUTES, attr);
        json_array_append_new(arr, obj);
    }

    return HttpResponse(MHD_HTTP_OK, mxs_json_resource(request.host(), "sql/odbc/drivers", arr));
}

HttpResponse etl_prepare(const HttpRequest& request)
{
    return run_etl_task<&sql_etl::ETL::prepare>(request);
}

HttpResponse etl_start(const HttpRequest& request)
{
    return run_etl_task<&sql_etl::ETL::start>(request);
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
    std::unique_ptr<HttpSql::ConnectionManager::Connection> elem;

    if (config.odbc_string.empty())
    {
        mxq::MariaDB conn;
        auto& sett = conn.connection_settings();
        sett.user = config.user;
        sett.password = config.password;
        sett.timeout = config.timeout;
        sett.ssl = config.ssl;
        sett.local_infile = false;

        if (config.proxy_protocol)
        {
            conn.set_local_text_proxy_header();
        }

        if (conn.open(config.host, config.port, config.db))
        {
            elem = std::make_unique<HttpSql::ConnectionManager::MariaDBConnection>(move(conn), config);
        }
        else
        {
            *err = conn.error();
        }
    }
    else
    {
        mxq::ODBC odbc(config.odbc_string, std::chrono::seconds{config.timeout});

        if (odbc.connect())
        {
            elem = std::make_unique<HttpSql::ConnectionManager::ODBCConnection>(move(odbc), config);
        }
        else
        {
            *err = odbc.error();
        }
    }

    if (elem)
    {
        id = this_unit.manager.add(move(elem));
    }

    return id;
}
}
