/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/sql_conn_manager.hh"
#include <maxbase/assert.hh>
#include <uuid/uuid.h>

using std::move;
using LockGuard = std::lock_guard<std::mutex>;

namespace HttpSql
{

ConnectionManager::Connection* ConnectionManager::get_connection(const std::string& id)
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

std::optional<ConnectionConfig> ConnectionManager::get_configuration(const std::string& id)
{
    std::optional<ConnectionConfig> rval;
    LockGuard guard(m_connection_lock);

    if (auto it = m_connections.find(id); it != m_connections.end())
    {
        rval = it->second->config;
    }

    return rval;
}

std::string ConnectionManager::add(mxq::MariaDB&& conn, const ConnectionConfig& cnf)
{
    auto elem = std::make_unique<Connection>(move(conn), cnf);

    LockGuard guard(m_connection_lock);

    uuid_t uuid;
    char uuid_str[37];      // 36 characters plus terminating null byte
    uuid_generate(uuid);
    uuid_unparse(uuid, uuid_str);

    m_connections.emplace(uuid_str, move(elem));
    return uuid_str;
}

bool ConnectionManager::erase(const std::string& id)
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

bool ConnectionManager::is_query(const std::string& conn_id, int64_t query_id) const
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

bool ConnectionManager::is_connection(const std::string& conn_id) const
{
    LockGuard guard(m_connection_lock);
    return m_connections.find(conn_id) != m_connections.end();
}

std::vector<std::string> ConnectionManager::get_connections()
{
    std::vector<std::string> conns;

    LockGuard guard(m_connection_lock);

    conns.reserve(m_connections.size());
    for (const auto& kv : m_connections)
    {
        conns.push_back(kv.first);
    }

    return conns;
}

json_t* ConnectionManager::connection_to_json(const std::string& conn_id)
{
    LockGuard guard(m_connection_lock);
    auto it = m_connections.find(conn_id);
    return it != m_connections.end() ? it->second->to_json() : nullptr;
}

void ConnectionManager::cleanup_thread_func()
{
    // TODO: make configurable?
    const auto idle_suspect_limit = mxb::from_secs(5 * 60);     // Ping these and close if ping fails.
    const auto check_interval = mxb::from_secs(5 * 60);

    auto should_stop_waiting = [this]() {
        return !m_keep_running.load(std::memory_order_acquire);
    };

    std::vector<std::string> suspect_idle_ids;

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
                if (idle_time > idle_suspect_limit && !managed_conn->conn.still_alive())
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
    // There are cases where the call to HttpSQL::stop_cleanup() is not done before shutdown. This mostly
    // happens when multiple termination signals are sent one after another and MaxScale is doing something
    // that is blocking the shutdown temporarily (e.g. blocking TCP connection).
    stop_cleanup_thread();
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

ConnectionManager::Connection::Connection(mxq::MariaDB&& new_conn, const ConnectionConfig& cnf)
    : conn(move(new_conn))
    , last_query_time(mxb::Clock::now())
    , config(cnf)
{
}

void ConnectionManager::Connection::release()
{
    busy.store(false, std::memory_order_release);
}

json_t* ConnectionManager::Connection::to_json() const
{
    auto idle = std::chrono::duration_cast<std::chrono::seconds>(mxb::Clock::now() - last_query_time);
    json_t* obj = json_object();
    json_object_set_new(obj, "thread_id", json_integer(conn.thread_id()));
    json_object_set_new(obj, "seconds_idle", json_integer(idle.count()));
    return obj;
}
}
