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

#pragma once

#include <map>
#include <mutex>
#include <vector>
#include <mysql.h>

namespace HttpSql
{

class ConnectionManager
{
public:
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;
    ConnectionManager() = default;

    struct Connection
    {
        MYSQL*  conn {nullptr};
        bool    expecting_result {false};
        int64_t query_id {0};
    };

    Connection get(int64_t id);
    int64_t    add(MYSQL* conn);
    void       put(int64_t id, Connection conn);

    bool is_query(int64_t conn_id, int64_t query_id) const;
    bool is_connection(int64_t conn_id) const;

    std::vector<int64_t> get_connections();

private:
    std::map<int64_t, Connection> m_connections;
    mutable std::mutex            m_connection_lock;
    int64_t                       m_id_gen {1};
};
}
