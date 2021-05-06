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

    struct Result
    {
        virtual ~Result() = default;

        virtual json_t* to_json() const = 0;
    };

    static HttpResponse connect(const HttpRequest& request);

    static HttpResponse show_connection(const HttpRequest& request);

    static HttpResponse show_all_connections(const HttpRequest& request);

    static HttpResponse query(const HttpRequest& request);

    static HttpResponse result(const HttpRequest& request);

    static HttpResponse disconnect(const HttpRequest& request);

    static bool is_query(const std::string& id);

    static bool is_connection(const std::string& id);

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

    // Helper for reading query results
    static HttpResponse read_query_result(int64_t id, const std::string& host, const std::string& self,
                                          const std::string& query_id, int64_t page_size);

    //
    // The functions that implement the connection creation and query execution
    //

    static std::vector<int64_t> get_connections();

    static int64_t create_connection(const ConnectionConfig& config, std::string* err);

    static int64_t execute_query(int64_t id, const std::string& sql, std::string* err);

    static std::vector<std::unique_ptr<Result>> read_result(int64_t id, int64_t rows_max, bool* more_results);

    static void close_connection(int64_t id);
};
