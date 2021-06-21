/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-06-21
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

#include <maxsql/mariadb_connector.hh>
#include <maxscale/server.hh>

namespace HttpSql
{

HttpResponse connect(const HttpRequest& request);
HttpResponse show_connection(const HttpRequest& request);
HttpResponse show_all_connections(const HttpRequest& request);
HttpResponse query(const HttpRequest& request);
HttpResponse disconnect(const HttpRequest& request);

bool is_query(const std::string& id);
bool is_connection(const std::string& id);

void start_cleanup();
void stop_cleanup();

//
// The functions that implement the connection creation and query execution
//

std::vector<int64_t> get_connections();

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

int64_t create_connection(const ConnectionConfig& config, std::string* err);
}
