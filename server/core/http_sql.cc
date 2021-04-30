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

int64_t get_connection_id(const HttpRequest& request)
{
    bool ok = false;
    std::string aud;
    int64_t id = 0;
    mxb::Json json(request.get_json());

    if (json.contains("connection_id"))
    {
        std::tie(ok, aud) = mxs::jwt::get_audience(json.get_string("conn_id"));
    }
    else
    {
        auto tok = request.get_cookie(CONN_ID_BODY) + request.get_cookie(CONN_ID_SIG);
        std::tie(ok, aud) = mxs::jwt::get_audience(tok);
    }

    if (ok)
    {
        id = strtol(aud.c_str(), nullptr, 10);
    }

    return id;
}

json_t* format_result(MYSQL* conn, int rc)
{
    json_t* rval = json_object();

    if (rc != 0)
    {
        json_object_set_new(rval, "errno", json_integer(mysql_errno(conn)));
        json_object_set_new(rval, "message", json_string(mysql_error(conn)));
        json_object_set_new(rval, "sqlstate", json_string(mysql_sqlstate(conn)));
    }
    else if (auto res = mysql_use_result(conn))
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

        json_object_set_new(rval, "data", data);
        json_object_set_new(rval, "fields", meta);
        mysql_free_result(res);
    }
    else
    {
        json_object_set_new(rval, "last_insert_id", json_integer(mysql_insert_id(conn)));
        json_object_set_new(rval, "warnings", json_integer(mysql_warning_count(conn)));
        json_object_set_new(rval, "affected_rows", json_integer(mysql_affected_rows(conn)));
    }

    return rval;
}

HttpResponse::Callback execute_query(const std::string& host, int port, const mxb::SSLConfig& ssl,
                                     const std::string& user, const std::string& pw, const std::string& db,
                                     const std::string& sql, unsigned int timeout)
{
    return [=]() {
               MYSQL* conn = mysql_init(nullptr);
               json_t* rval = nullptr;

               mysql_optionsv(conn, MYSQL_OPT_READ_TIMEOUT, &timeout);
               mysql_optionsv(conn, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
               mysql_optionsv(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

               if (mxs_mysql_real_connect(conn, host.c_str(), port, user.c_str(), pw.c_str(), ssl,
                                          CLIENT_MULTI_RESULTS | CLIENT_MULTI_STATEMENTS))
               {
                   if (!db.empty() && mysql_query(conn, ("USE `" + db + "`").c_str()) != 0)
                   {
                       rval = format_result(conn, 1);
                   }
                   else
                   {
                       std::vector<json_t*> results;
                       bool again = false;
                       int rc = mysql_query(conn, sql.c_str());
                       results.push_back(format_result(conn, rc));

                       while (rc == 0 && mysql_more_results(conn))
                       {
                           rc = mysql_next_result(conn);
                           results.push_back(format_result(conn, rc));
                       }

                       mxb_assert(!results.empty());

                       if (results.size() > 1)
                       {
                           rval = json_array();

                           for (json_t* res : results)
                           {
                               json_array_append_new(rval, res);
                           }
                       }
                       else
                       {
                           rval = results.front();
                       }
                   }
               }
               else
               {
                   rval = format_result(conn, 1);
               }

               mysql_close(conn);
               return HttpResponse(MHD_HTTP_OK, rval);
           };
}
}

// static
HttpResponse HttpSql::connect(const HttpRequest& request)
{
    mxb::Json json(request.get_json());
    std::string user;
    std::string pw;
    std::string db;
    int64_t timeout = 10;
    int64_t id = get_connection_id(request);

    json.try_get_int("timeout", &timeout);
    json.try_get_string("db", &db);

    if (!json.try_get_string("user", &user) || !json.try_get_string("password", &pw))
    {
        return HttpResponse(MHD_HTTP_FORBIDDEN, mxs_json_error("Missing `user` or `password`."));
    }

    auto server = ServerManager::find_by_unique_name(request.uri_part(1));

    if (id)
    {
        //
        // TODO: Close the old connection
        //
    }

    //
    // TODO: Create the connection
    //

    // TODO: Figure out how long the connections should be kept valid
    int max_age = 28800;
    auto token = mxs::jwt::create("mxs-query", std::to_string(id), max_age);

    if (request.get_option("persist") == "yes")
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

// static
HttpResponse HttpSql::query(const HttpRequest& request)
{
    mxb::Json json(request.get_json());
    std::string user;
    std::string pw;
    std::string sql;
    std::string db = json.get_string("db");
    int64_t timeout = 10;
    json.try_get_int("timeout", &timeout);

    if (!json.try_get_string("user", &user)
        || !json.try_get_string("password", &pw)
        || !json.try_get_string("sql", &sql))
    {
        return HttpResponse(MHD_HTTP_FORBIDDEN,
                            mxs_json_error("Missing one of `user`, `password` or `sql`."));
    }

    // TODO: Use the ID to find the open connection
    int64_t id = get_connection_id(request);

    auto server = ServerManager::find_by_unique_name(request.uri_part(1));
    return execute_query(server->address(), server->port(), server->ssl_config(), user, pw, db, sql, timeout);
}

// static
HttpResponse HttpSql::disconnect(const HttpRequest& request)
{
    int64_t id = get_connection_id(request);

    //
    // TODO: Close the connection
    //

    return HttpResponse(MHD_HTTP_NO_CONTENT);
}
