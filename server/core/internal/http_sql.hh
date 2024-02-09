/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
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
#include "sql_conn_manager.hh"

#include <maxsql/mariadb_connector.hh>
#include <maxscale/server.hh>

namespace HttpSql
{

HttpResponse connect(const HttpRequest& request);
HttpResponse reconnect(const HttpRequest& request);
HttpResponse clone(const HttpRequest& request);
HttpResponse show_connection(const HttpRequest& request);
HttpResponse show_all_connections(const HttpRequest& request);
HttpResponse query(const HttpRequest& request);
HttpResponse query_result(const HttpRequest& request);
HttpResponse erase_query_result(const HttpRequest& request);
HttpResponse disconnect(const HttpRequest& request);
HttpResponse cancel(const HttpRequest& request);
HttpResponse etl_prepare(const HttpRequest& request);
HttpResponse etl_start(const HttpRequest& request);
HttpResponse odbc_drivers(const HttpRequest& request);

bool is_query(const std::string& id);
bool is_connection(const std::string& id);

void init();
void finish();

//
// The functions that implement the connection creation and query execution
//

std::vector<std::string> get_connections();

std::string create_connection(const ConnectionConfig& config, std::string* err);
}
