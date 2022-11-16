/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/sql_conn_manager.hh"
#include <maxbase/assert.hh>
#include <maxbase/json.hh>
#include <uuid/uuid.h>

using std::move;
using LockGuard = std::lock_guard<std::mutex>;

namespace HttpSql
{

std::tuple<ConnectionManager::Connection*, ConnectionManager::Reason, std::string>
ConnectionManager::get_connection(const std::string& id)
{
    Connection* rval = nullptr;
    Reason reason = Reason::NOT_FOUND;
    std::string sql;
    LockGuard guard(m_connection_lock);
    auto it = m_connections.find(id);
    if (it != m_connections.end())
    {
        reason = Reason::BUSY;
        auto elem = it->second.get();
        sql = elem->sql;
        if (!elem->busy.load(std::memory_order_acquire))
        {
            reason = Reason::OK;
            rval = elem;
            elem->busy.store(true, std::memory_order_release);
        }
    }
    return {rval, reason, sql};
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

std::string ConnectionManager::add(std::unique_ptr<Connection> elem)
{
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
    const auto idle_hard_limit = mxb::from_secs(60 * 60);       // Close these unconditionally.
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
            if (auto [managed_conn, reason, _] = get_connection(id); managed_conn)
            {
                // It's possible that the connection was used just after the previous loop, so check again.
                bool should_close = false;
                auto idle_time = now - managed_conn->last_query_time;
                // TODO: If auto-reconnection is ever enabled on the connector, may need to detect
                // it happening. To do that, check if mysql thread id changes during 'ping'.
                if ((idle_time > idle_hard_limit)
                    || (idle_time > idle_suspect_limit && !managed_conn->ping()))
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

ConnectionManager::Connection::Connection(const ConnectionConfig& cnf)
    : last_query_time(mxb::Clock::now())
    , config(cnf)
{
}

void ConnectionManager::Connection::release()
{
    busy.store(false, std::memory_order_release);
}

json_t* ConnectionManager::Connection::to_json() const
{
    auto now = mxb::Clock::now();
    double idle = 0;
    double exec_time = 0;

    if (busy.load(std::memory_order_acquire))
    {
        exec_time = mxb::to_secs(now - last_query_started);
    }
    else
    {
        exec_time = mxb::to_secs(last_query_time - last_query_started);
        idle = mxb::to_secs(now - last_query_time);
    }

    json_t* obj = json_object();
    json_object_set_new(obj, "thread_id", json_integer(thread_id()));
    json_object_set_new(obj, "seconds_idle", json_real(idle));
    json_object_set_new(obj, "sql", json_string(sql.c_str()));
    json_object_set_new(obj, "execution_time", json_real(exec_time));
    json_object_set_new(obj, "target", json_string(config.target.c_str()));
    return obj;
}

ConnectionManager::MariaDBConnection::MariaDBConnection(mxq::MariaDB&& new_conn, const ConnectionConfig& cnf)
    : Connection(cnf)
    , m_conn(std::move(new_conn))
{
}

std::string ConnectionManager::MariaDBConnection::error()
{
    return m_conn.error();
}

bool ConnectionManager::MariaDBConnection::cmd(const std::string& cmd)
{
    return m_conn.cmd(cmd);
}

mxb::Json ConnectionManager::MariaDBConnection::query(const std::string& sql, int64_t max_rows)
{
    if (this->last_max_rows != max_rows)
    {
        std::string limit = max_rows ? std::to_string(max_rows) : "DEFAULT";
        this->cmd("SET sql_select_limit=" + limit);
        this->last_max_rows = max_rows ? max_rows : std::numeric_limits<int64_t>::max();
    }

    m_conn.streamed_query(sql);
    return generate_json_representation(last_max_rows);
}

uint32_t ConnectionManager::MariaDBConnection::thread_id() const
{
    return m_conn.thread_id();
}

bool ConnectionManager::MariaDBConnection::reconnect()
{
    return m_conn.reconnect();
}

bool ConnectionManager::MariaDBConnection::ping()
{
    return m_conn.ping();
}

json_t* ConnectionManager::MariaDBConnection::generate_column_info(
    const mxq::MariaDBQueryResult::Fields& fields_info)
{
    json_t* rval = json_array();
    for (auto& elem : fields_info)
    {
        json_array_append_new(rval, json_string(elem.name.c_str()));
    }
    return rval;
}

json_t* ConnectionManager::MariaDBConnection::generate_resultdata_row(mxq::MariaDBQueryResult* resultset,
                                                                      const mxq::MariaDBQueryResult::Fields& field_info)
{
    using Type = mxq::MariaDBQueryResult::Field::Type;
    json_t* rval = json_array();
    auto n = field_info.size();
    auto rowdata = resultset->rowdata();

    for (size_t i = 0; i < n; i++)
    {
        json_t* value = nullptr;

        if (rowdata[i])
        {
            switch (field_info[i].type)
            {
            case Type::INTEGER:
                value = json_integer(strtol(rowdata[i], nullptr, 10));
                break;

            case Type::FLOAT:
                value = json_real(strtod(rowdata[i], nullptr));
                break;

            case Type::NUL:
                value = json_null();
                break;

            default:
                value = json_string(rowdata[i]);
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

        json_array_append_new(rval, value);
    }
    return rval;
}

mxb::Json ConnectionManager::MariaDBConnection::generate_json_representation(int64_t max_rows)
{
    using ResultType = mxq::MariaDB::ResultType;
    json_t* resultset_arr = json_array();

    auto current_type = m_conn.current_result_type();
    while (current_type != ResultType::NONE)
    {
        switch (current_type)
        {
        case ResultType::OK:
            {
                auto res = m_conn.get_ok_result();
                json_t* ok = json_object();
                json_object_set_new(ok, "last_insert_id", json_integer(res->insert_id));
                json_object_set_new(ok, "warnings", json_integer(res->warnings));
                json_object_set_new(ok, "affected_rows", json_integer(res->affected_rows));
                json_array_append_new(resultset_arr, ok);
            }
            break;

        case ResultType::ERROR:
            {
                auto res = m_conn.get_error_result();
                json_t* err = json_object();
                json_object_set_new(err, "errno", json_integer(res->error_num));
                json_object_set_new(err, "message", json_string(res->error_msg.c_str()));
                json_object_set_new(err, "sqlstate", json_string(res->sqlstate.c_str()));
                json_array_append_new(resultset_arr, err);
            }
            break;

        case ResultType::RESULTSET:
            {
                auto res = m_conn.get_resultset();
                auto fields = res->fields();
                json_t* resultset = json_object();
                json_t* rows = json_array();
                int64_t rows_read = 0;

                // We have to read the whole resultset in order to find whether it ended with an error
                while (res->next_row())
                {
                    if (rows_read++ < max_rows)
                    {
                        json_array_append_new(rows, generate_resultdata_row(res.get(), fields));
                    }
                }

                auto error = m_conn.get_error_result();

                if (error->error_num)
                {
                    json_object_set_new(resultset, "errno", json_integer(error->error_num));
                    json_object_set_new(resultset, "message", json_string(error->error_msg.c_str()));
                    json_object_set_new(resultset, "sqlstate", json_string(error->sqlstate.c_str()));
                    json_decref(rows);
                }
                else
                {
                    json_object_set_new(resultset, "data", rows);
                    json_object_set_new(resultset, "fields", generate_column_info(fields));
                    json_object_set_new(resultset, "complete", json_boolean(rows_read < max_rows));
                }

                json_array_append_new(resultset_arr, resultset);
            }
            break;

        case ResultType::NONE:
            break;
        }
        current_type = m_conn.next_result();
    }

    return mxb::Json(resultset_arr, mxb::Json::RefType::STEAL);
}
}
