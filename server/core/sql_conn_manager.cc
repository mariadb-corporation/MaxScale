/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/sql_conn_manager.hh"
#include <maxbase/assert.h>

using std::move;
using LockGuard = std::lock_guard<std::mutex>;

namespace HttpSql
{

ConnectionManager::Connection* ConnectionManager::get_connection(int64_t id)
{
    Connection* rval = nullptr;
    LockGuard guard(m_connection_lock);
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
    auto elem = std::make_unique<Connection>(move(conn));

    LockGuard guard(m_connection_lock);
    int64_t id = m_next_id++;
    m_connections.emplace(id, move(elem));
    return id;
}

bool ConnectionManager::erase(int64_t id)
{
    bool rval = false;
    LockGuard guard(m_connection_lock);
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
    LockGuard guard(m_connection_lock);
    auto it = m_connections.find(conn_id);

    if (it != m_connections.end())
    {
        rval = query_id == it->second->current_query_id;
    }

    return rval;
}

bool ConnectionManager::is_connection(int64_t conn_id) const
{
    LockGuard guard(m_connection_lock);
    return m_connections.find(conn_id) != m_connections.end();
}

std::vector<int64_t> ConnectionManager::get_connections()
{
    std::vector<int64_t> conns;

    LockGuard guard(m_connection_lock);

    conns.reserve(m_connections.size());
    for (const auto& kv : m_connections)
    {
        conns.push_back(kv.first);
    }

    return conns;
}

void ConnectionManager::cleanup_thread_func()
{
    // TODO: make configurable?
    const auto idle_suspect_limit = mxb::from_secs(5 * 60);     // Ping these and close if ping fails.
    const auto idle_hard_limit = mxb::from_secs(60 * 60);       // Close these unconditionally.
    const auto check_interval = mxb::from_secs(5 * 60);

    auto should_stop_waiting = [this]() {
            return !m_keep_running.load(std::memory_order_acquire);
        };

    std::vector<int64_t> suspect_idle_ids;

    while (m_keep_running)
    {
        auto now = mxb::Clock::now();

        // We don't want to keep the connections-mutex locked during a cleanup-pass, as it involves blocking
        // I/O. So, first collect thread-id:s from currently idle looking connections to a separate map.
        {
            LockGuard guard(m_connection_lock);
            for (auto& kv : m_connections)
            {
                auto* managed_conn = kv.second.get();
                // Assume idle_hard_limit > idle_suspect_limit.
                if (!managed_conn->busy.load(std::memory_order_acquire)
                    && (now - managed_conn->last_query_time > idle_suspect_limit))
                {
                    suspect_idle_ids.push_back(kv.first);
                }
            }
        }

        for (auto id : suspect_idle_ids)
        {
            auto managed_conn = get_connection(id);
            if (managed_conn)
            {
                // It's possible that the connection was used just after the previous loop, so check again.
                bool should_close = false;
                auto idle_time = now - managed_conn->last_query_time;
                // TODO: If auto-reconnection is ever enabled on the connector, may need to detect
                // it happening. To do that, check if mysql thread id changes during 'ping'.
                if ((idle_time > idle_hard_limit)
                    || (idle_time > idle_suspect_limit && !managed_conn->conn.ping()))
                {
                    should_close = true;
                }

                // Release the connection, then erase. In theory, the connection may become active
                // between the two calls, however, that would just cause a failed erase.
                managed_conn->release();
                if (should_close)
                {
                    erase(id);
                }
            }
        }
        suspect_idle_ids.clear();

        auto next_check = mxb::Clock::now() + check_interval;
        std::unique_lock<std::mutex> lock(m_notifier_lock);
        m_stop_running_notifier.wait_until(lock, next_check, should_stop_waiting);
    }
}

ConnectionManager::~ConnectionManager()
{
    mxb_assert(!m_cleanup_thread.joinable());
}

void ConnectionManager::start_cleanup_thread()
{
    m_cleanup_thread = std::thread(&ConnectionManager::cleanup_thread_func, this);
}

void ConnectionManager::stop_cleanup_thread()
{
    {
        LockGuard guard(m_connection_lock);
        m_keep_running = false;
    }

    // The cleanup thread may not have been created if MaxScale start failed.
    if (m_cleanup_thread.joinable())
    {
        m_stop_running_notifier.notify_one();
        m_cleanup_thread.join();
    }
}

ConnectionManager::Connection::~Connection()
{
    // Should only delete idle connections. If this condition cannot be guaranteed, use shared_ptr.
    mxb_assert(!busy);
}

ConnectionManager::Connection::Connection(mxq::MariaDB&& new_conn)
    : conn(move(new_conn))
    , last_query_time(mxb::Clock::now())
{
}

void ConnectionManager::Connection::release()
{
    busy.store(false, std::memory_order_release);
}
}
