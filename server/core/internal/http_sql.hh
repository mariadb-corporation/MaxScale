/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

//
// REST API interface for SQL queries
//

#include "httprequest.hh"
#include "httpresponse.hh"

#include <maxscale/server.hh>

class HttpSql
{
public:
    static HttpResponse connect(const HttpRequest& request);

    static HttpResponse query(const HttpRequest& request);

    static HttpResponse disconnect(const HttpRequest& request);

private:

    struct ConnectionConfig
    {
        std::string    host;
        int            port;
        std::string    user;
        std::string    password;
        std::string    db;
        int64_t        timeout = 10;
        mxb::SSLConfig ssl;
    };

    //
    // The functions that implement the connection creation and query execution
    //

    static int64_t create_connection(const ConnectionConfig& config, std::string* err);

    static bool execute_query(int64_t id, const std::string& sql);

    static bool read_result(int64_t id);

    static void close_connection(int64_t id);
};
