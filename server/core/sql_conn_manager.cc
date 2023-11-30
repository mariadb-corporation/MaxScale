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
#include <maxbase/json.hh>
#include <uuid/uuid.h>
#include <maxbase/string.hh>
#include <maxsimd/multistmt.hh>

using std::move;
using LockGuard = std::lock_guard<std::mutex>;

namespace
{
std::string wrap_in_atomic_block(const std::string& sql)
{
    if (!maxsimd::is_multi_stmt(sql))
    {
        return sql;
    }

    bool have_semicolon = false;

    for (auto it = sql.rbegin(); it != sql.rend(); it++)
    {
        if (!isspace(*it))
        {
            have_semicolon = *it == ';';
            break;
        }
    }

    std::ostringstream ss;
    ss << "BEGIN NOT ATOMIC " << sql << (have_semicolon ? "" : ";") << "END";
    return ss.str();
}

int parse_version(std::string str)
{
    int ver = 0;

    if (auto tok = mxb::strtok(str, "."); tok.size() == 3)
    {
        ver = strtol(tok[0].c_str(), nullptr, 10) * 10000;
        ver += strtol(tok[1].c_str(), nullptr, 10) * 100;
        ver += strtol(tok[2].c_str(), nullptr, 10);
    }

    return ver;
}
}

namespace HttpSql
{

std::tuple<ConnectionManager::Connection*, ConnectionManager::Reason, ConnectionManager::Connection::Info>
ConnectionManager::get_connection(const std::string& id)
{
    Connection* rval = nullptr;
    Reason reason = Reason::NOT_FOUND;
    Connection::Info info;
    LockGuard guard(m_connection_lock);
    auto it = m_connections.find(id);
    if (it != m_connections.end())
    {
        reason = Reason::BUSY;
        auto elem = it->second.get();

        if (!elem->busy.load(std::memory_order_acquire))
        {
            reason = Reason::OK;
            rval = elem;
            info = elem->info();
            elem->busy.store(true, std::memory_order_release);
        }
        else
        {
            std::lock_guard elem_guard(elem->m_lock);
            info = elem->info();

            if (elem->m_status_handler)
            {
                info.status = elem->m_status_handler();
            }
        }
    }
    return {rval, reason, info};
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

bool ConnectionManager::cancel(const std::string& id)
{
    bool rval = false;
    LockGuard guard(m_connection_lock);

    if (auto it = m_connections.find(id); it != m_connections.end())
    {
        rval = true;

        if (it->second->busy.load(std::memory_order_acquire))
        {
            it->second->cancel();
        }
    }

    return rval;
}

void ConnectionManager::Connection::cancel()
{
    do_cancel();

    std::lock_guard guard(m_lock);

    if (m_cancel_handler)
    {
        m_cancel_handler();
    }
}

void ConnectionManager::Connection::set_cancel_handler(std::function<void()> fn)
{
    std::lock_guard guard(m_lock);
    m_cancel_handler = std::move(fn);
}

void ConnectionManager::Connection::clear_cancel_handler()
{
    std::lock_guard guard(m_lock);
    m_cancel_handler = nullptr;
}

void ConnectionManager::Connection::set_status_handler(std::function<mxb::Json()> fn)
{
    std::lock_guard guard(m_lock);
    m_status_handler = std::move(fn);
}

void ConnectionManager::Connection::clear_status_handler()
{
    std::lock_guard guard(m_lock);
    m_status_handler = nullptr;
}

void ConnectionManager::Connection::query_start(const std::string& sql)
{
    // The update to the m_info struct must be done under a lock as other threads can access it concurrently
    // from inside ConnectionManager::get_connection(). Although the last_query_started value is never
    // accessed by more than one thread at a time, it should still be updated to make sure that the end time
    // is always ahead or at the same point than the start time.
    std::unique_lock guard(m_lock);
    m_info.sql = sql;
    m_info.last_query_started = mxb::Clock::now();
    m_info.last_query_ended = m_info.last_query_started;
    guard.unlock();
}

mxb::Json ConnectionManager::Connection::query(const std::string& sql, int64_t max_rows, int64_t timeout)
{
    query_start(sql);
    auto result = do_query(sql, max_rows, timeout);
    query_end();

    return result;
}

void ConnectionManager::Connection::query_end()
{
    m_info.last_query_ended = mxb::Clock::now();
}

const ConnectionManager::Connection::Info& ConnectionManager::Connection::info() const
{
    return m_info;
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
    const auto idle_suspect_limit = mxb::from_secs(5 * 60);
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
                    && (now - managed_conn->info().last_query_ended > idle_suspect_limit))
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
                auto idle_time = now - managed_conn->info().last_query_ended;
                if (idle_time > idle_suspect_limit && !managed_conn->still_alive())
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
    // There are cases where the call to HttpSQL::finish() is not done before shutdown. This mostly
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

void ConnectionManager::cancel_all_connections()
{
    LockGuard guard(m_connection_lock);

    for (const auto& [id, conn] : m_connections)
    {
        if (conn->busy.load(std::memory_order_acquire))
        {
            conn->cancel();
        }
    }
}

ConnectionManager::Connection::~Connection()
{
    // Should only delete idle connections. If this condition cannot be guaranteed, use shared_ptr.
    mxb_assert(!busy);
}

ConnectionManager::Connection::Connection(const ConnectionConfig& cnf)
    : config(cnf)
{
    m_info.last_query_ended = mxb::Clock::now();
    m_info.last_query_started = m_info.last_query_ended;
}

void ConnectionManager::Connection::release()
{
    busy.store(false, std::memory_order_release);
}

json_t* ConnectionManager::Connection::to_json() const
{
    auto now = mxb::Clock::now();
    double idle = 0;
    std::string sql;

    if (busy.load(std::memory_order_acquire))
    {
        std::lock_guard guard(m_lock);
        sql = m_info.sql;
    }
    else
    {
        idle = mxb::to_secs(now - m_info.last_query_ended);
        sql = m_info.sql;
    }

    json_t* obj = json_object();
    json_object_set_new(obj, "thread_id", json_integer(thread_id()));
    json_object_set_new(obj, "seconds_idle", json_real(idle));
    json_object_set_new(obj, "sql", sql.empty() ? json_null() : json_string(sql.c_str()));
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

mxb::Json ConnectionManager::MariaDBConnection::do_query(const std::string& sql,
                                                         int64_t max_rows,
                                                         int64_t timeout)
{
    if (this->last_max_rows != max_rows)
    {
        std::string limit = max_rows ? std::to_string(max_rows) : "DEFAULT";
        this->cmd("SET sql_select_limit=" + limit);
        this->last_max_rows = max_rows ? max_rows : std::numeric_limits<int64_t>::max();
    }

    if (this->last_timeout != timeout)
    {
        // Use a slightly longer timeout for the connector's network timeouts. This way the server will kill
        // the query before the network read times out and we get a better error message.
        m_conn.set_timeout(timeout ? timeout + 1 : std::numeric_limits<int>::max());
        std::string limit = timeout ? std::to_string(timeout) : "DEFAULT";
        this->cmd("SET max_statement_time=" + limit);
        this->last_timeout = timeout ? timeout : std::numeric_limits<int64_t>::max();
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

bool ConnectionManager::MariaDBConnection::still_alive()
{
    return m_conn.still_alive();
}

void ConnectionManager::MariaDBConnection::do_cancel()
{
    mxq::MariaDB other;
    auto& sett = other.connection_settings();
    sett.user = config.user;
    sett.password = config.password;
    sett.timeout = config.timeout;
    sett.ssl = config.ssl;
    if (config.proxy_protocol)
    {
        other.set_local_text_proxy_header();
    }

    if (other.open(config.host, config.port, config.db))
    {
        other.cmd("KILL QUERY " + std::to_string(thread_id()));
    }
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

ConnectionManager::ODBCConnection::ODBCConnection(mxq::ODBC&& odbc, const ConnectionConfig& cnf)
    : Connection(cnf)
    , m_conn(move(odbc))
{
    if (m_conn.driver_name() == "libmaodbc.so")
    {
        if (parse_version(m_conn.driver_version()) < 30118)
        {
            // This is a workaround for ODBC-375: https://jira.mariadb.org/browse/ODBC-375
            m_wrap_in_atomic_block = true;
        }
    }
}

std::string ConnectionManager::ODBCConnection::error()
{
    return m_conn.error();
}

bool ConnectionManager::ODBCConnection::cmd(const std::string& cmd)
{
    mxq::NoResult empty;
    return m_conn.query(cmd, &empty);
}

mxb::Json ConnectionManager::ODBCConnection::do_query(const std::string& sql,
                                                      int64_t max_rows,
                                                      int64_t timeout)
{
    mxq::JsonResult res;
    m_conn.set_row_limit(max_rows);
    m_conn.set_query_timeout(std::chrono::seconds {timeout});
    std::string final_sql = m_wrap_in_atomic_block ? wrap_in_atomic_block(sql) : sql;
    m_conn.query(final_sql, &res);
    auto result = res.result();
    mxb_assert(result.type() == mxb::Json::Type::ARRAY);

    return result;
}

uint32_t ConnectionManager::ODBCConnection::thread_id() const
{
    // Not applicable to ODBC connections
    return 0;
}

bool ConnectionManager::ODBCConnection::reconnect()
{
    m_conn.disconnect();
    return m_conn.connect();
}

bool ConnectionManager::ODBCConnection::still_alive()
{
    // TODO: This will prevent any idle timeouts like wait_timeout from working
    mxq::NoResult empty;
    return m_conn.query("SELECT 1", &empty);
}

void ConnectionManager::ODBCConnection::do_cancel()
{
    m_conn.cancel();
}
}
