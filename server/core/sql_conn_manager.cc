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

#include "internal/sql_conn_manager.hh"

namespace HttpSql
{

ConnectionManager::Connection ConnectionManager::get(int64_t id)
{
    Connection conn;
    std::lock_guard<std::mutex> guard(m_connection_lock);
    auto it = m_connections.find(id);

    if (it != m_connections.end())
    {
        conn = it->second;
        m_connections.erase(it);
    }

    return conn;
}

int64_t ConnectionManager::add(MYSQL* conn)
{
    std::lock_guard<std::mutex> guard(m_connection_lock);
    int64_t id = m_id_gen++;
    Connection c {conn, false};
    m_connections.emplace(id, c);

    if (m_id_gen <= 0)
    {
        m_id_gen = 1;
    }

    return id;
}

void ConnectionManager::put(int64_t id, ConnectionManager::Connection conn)
{
    std::lock_guard<std::mutex> guard(m_connection_lock);
    m_connections.emplace(id, conn);
}

bool ConnectionManager::is_query(int64_t conn_id, int64_t query_id) const
{
    bool rval = false;
    std::lock_guard<std::mutex> guard(m_connection_lock);
    auto it = m_connections.find(conn_id);

    if (it != m_connections.end())
    {
        rval = query_id == it->second.query_id;
    }

    return rval;
}

bool ConnectionManager::is_connection(int64_t conn_id) const
{
    std::lock_guard<std::mutex> guard(m_connection_lock);
    return m_connections.find(conn_id) != m_connections.end();
}

std::vector<int64_t> ConnectionManager::get_connections()
{
    std::lock_guard<std::mutex> guard(m_connection_lock);
    std::vector<int64_t> conns;

    for (const auto& kv : m_connections)
    {
        conns.push_back(kv.first);
    }

    return conns;
}
}
