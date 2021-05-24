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
#include <maxbase/assert.h>

using std::move;

namespace HttpSql
{

ConnectionManager::Connection* ConnectionManager::get_connection(int64_t id)
{
    Connection* rval = nullptr;
    std::lock_guard<std::mutex> guard(m_connection_lock);
    auto it = m_connections.find(id);
    if (it != m_connections.end())
    {
        auto elem = it->second.get();
        if (!elem->busy.load(std::memory_order_acquire))
        {
            rval = elem;
            elem->busy.store(true, std::memory_order_release);
        }
    }
    return rval;
}

int64_t ConnectionManager::add(mxq::MariaDB&& conn)
{
    auto elem = std::make_unique<Connection>();
    elem->conn = move(conn);

    std::lock_guard<std::mutex> guard(m_connection_lock);
    int64_t id = m_next_id++;
    m_connections.emplace(id, std::move(elem));
    return id;
}

bool ConnectionManager::erase(int64_t id)
{
    bool rval = false;
    std::lock_guard<std::mutex> guard(m_connection_lock);
    auto it = m_connections.find(id);
    if (it != m_connections.end())
    {
        if (!it->second->busy.load(std::memory_order_acquire))
        {
            m_connections.erase(it);
            rval = true;
        }
    }
    return rval;
}

bool ConnectionManager::is_query(int64_t conn_id, int64_t query_id) const
{
    bool rval = false;
    std::lock_guard<std::mutex> guard(m_connection_lock);
    auto it = m_connections.find(conn_id);

    if (it != m_connections.end())
    {
        rval = query_id == it->second->current_query_id;
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

ConnectionManager::Connection::~Connection()
{
    // Should only delete idle connections. If this condition cannot be guaranteed, use shared_ptr.
    mxb_assert(!busy);
}
}
