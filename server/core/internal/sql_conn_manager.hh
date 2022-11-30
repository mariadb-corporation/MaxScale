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

#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <optional>

#include <maxbase/json.hh>
#include <maxbase/stopwatch.hh>
#include <maxsql/mariadb_connector.hh>
#include <maxsql/odbc.hh>

namespace HttpSql
{

struct ConnectionConfig
{
    std::string    target;
    std::string    host;
    int            port;
    std::string    user;
    std::string    password;
    std::string    db;
    int64_t        timeout = 10;
    bool           proxy_protocol = false;
    mxb::SSLConfig ssl;
    std::string    odbc_string;
};

class ConnectionManager
{
public:
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;
    ConnectionManager() = default;
    ~ConnectionManager();

    class Connection
    {
    public:
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;

        Connection(const ConnectionConfig& cnf);
        virtual ~Connection();
        void release();

        json_t* to_json() const;

        virtual std::string error() = 0;

        virtual bool cmd(const std::string& cmd) = 0;

        virtual mxb::Json query(const std::string& sql, int64_t max_rows) = 0;

        virtual uint32_t thread_id() const = 0;

        virtual bool reconnect() = 0;

        virtual bool ping() = 0;

        std::atomic_bool busy {false};
        int64_t          current_query_id {0};
        mxb::TimePoint   last_query_started;
        mxb::TimePoint   last_query_time;
        int64_t          last_max_rows {0};
        ConnectionConfig config;
        std::string      sql;
        mxb::Json        result {mxb::Json::Type::UNDEFINED};
    };

    class MariaDBConnection : public Connection
    {
    public:
        MariaDBConnection(mxq::MariaDB&& new_conn, const ConnectionConfig& cnf);
        std::string error() override final;
        bool        cmd(const std::string& cmd) override final;
        mxb::Json   query(const std::string& sql, int64_t max_rows) override final;
        uint32_t    thread_id() const override final;
        bool        reconnect() override final;
        bool        ping() override final;

    private:
        mxb::Json generate_json_representation(int64_t max_rows);
        json_t*   generate_column_info(const mxq::MariaDBQueryResult::Fields& fields_info);
        json_t*   generate_resultdata_row(mxq::MariaDBQueryResult* resultset,
                                          const mxq::MariaDBQueryResult::Fields& field_info);

        mxq::MariaDB m_conn;
    };

    class ODBCConnection : public Connection
    {
    public:
        ODBCConnection(mxq::ODBC&& odbc, const ConnectionConfig& cnf);

        std::string error() override final;
        bool        cmd(const std::string& cmd) override final;
        mxb::Json   query(const std::string& sql, int64_t max_rows) override final;
        uint32_t    thread_id() const override final;
        bool        reconnect() override final;
        bool        ping() override final;

    private:
        mxq::ODBC m_conn;
        bool      m_wrap_in_atomic_block {false};
    };

    enum class Reason
    {
        OK,
        NOT_FOUND,
        BUSY
    };

    /**
     * Get a connection by id and set the connection state to busy. Once the caller is done with
     * the connection, they should call 'release', allowing the connection
     * to be used again.
     *
     * @param id Connection id
     *
     * @return Pointer to connection if found not busy, null otherwise. The Reason explains why a nullptr was
     *         returned. If the connection is busy executing a SQL query, the query is also returned.
     */
    std::tuple<Connection*, Reason, std::string> get_connection(const std::string& id);

    /**
     * Get the configuration of a connection
     *
     * @param id The connection ID
     *
     * @return The configuration of the given connection if one with the given ID exists
     */
    std::optional<ConnectionConfig> get_configuration(const std::string& id);

    /**
     * Add a connection to the map.
     *
     * @param conn Existing connection
     */
    std::string add(std::unique_ptr<Connection> conn);

    /**
     * Erase a connection from the map.
     *
     * @param id Id of connection to erase
     * @return True if erased. False if id not found or was busy.
     */
    bool erase(const std::string& id);

    bool is_query(const std::string& conn_id, int64_t query_id) const;
    bool is_connection(const std::string& conn_id) const;

    std::vector<std::string> get_connections();

    json_t* connection_to_json(const std::string& conn_id);

    void start_cleanup_thread();
    void stop_cleanup_thread();

private:
    mutable std::mutex m_connection_lock;   /**< Protects access to connections map and next id */

    std::map<std::string, std::unique_ptr<Connection>> m_connections;   /**< Connections by id */

    // Fields for controlling the cleanup thread.
    std::atomic_bool        m_keep_running {true};
    std::condition_variable m_stop_running_notifier;
    std::mutex              m_notifier_lock;
    std::thread             m_cleanup_thread;

    void cleanup_thread_func();
};
}
