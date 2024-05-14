/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
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

        struct Info
        {
            mxb::TimePoint last_query_started;
            mxb::TimePoint last_query_ended;
            std::string    sql;
            mxb::Json      status;
        };

        Connection(const ConnectionConfig& cnf);
        virtual ~Connection();
        void release();

        json_t* to_json() const;

        virtual std::string error() = 0;

        virtual bool cmd(const std::string& cmd) = 0;

        mxb::Json query(const std::string& sql, int64_t max_rows, int64_t timeout);

        virtual uint32_t thread_id() const = 0;

        virtual bool reconnect() = 0;

        virtual bool still_alive() = 0;

        void cancel();

        void query_start(const std::string& sql);

        void query_end();

        void set_cancel_handler(std::function<void()> fn);

        void clear_cancel_handler();

        void set_status_handler(std::function<mxb::Json()> fn);

        void clear_status_handler();

        const Info& info() const;

        std::atomic_bool busy {false};
        int64_t          current_query_id {0};
        ConnectionConfig config;
        mxb::Json        result {mxb::Json::Type::UNDEFINED};

    protected:
        virtual mxb::Json do_query(const std::string& sql, int64_t max_rows, int64_t timeout) = 0;
        virtual void      do_cancel() = 0;

    private:
        friend class ConnectionManager;

        mutable std::mutex         m_lock;
        std::function<void()>      m_cancel_handler;
        std::function<mxb::Json()> m_status_handler;
        Info                       m_info;
    };

    class MariaDBConnection : public Connection
    {
    public:
        MariaDBConnection(mxq::MariaDB&& new_conn, const ConnectionConfig& cnf);
        std::string error() override final;
        bool        cmd(const std::string& cmd) override final;
        uint32_t    thread_id() const override final;
        bool        reconnect() override final;
        bool        still_alive() override final;

    protected:
        mxb::Json do_query(const std::string& sql, int64_t max_rows, int64_t timeout) override final;
        void      do_cancel() override final;

    private:
        mxb::Json generate_json_representation(int64_t max_rows);
        json_t*   generate_column_info(const mxq::MariaDBQueryResult::Fields& fields_info);
        json_t*   generate_column_metadata(const mxq::MariaDBQueryResult::Fields& fields_info);
        json_t*   generate_resultdata_row(mxq::MariaDBQueryResult* resultset,
                                          const mxq::MariaDBQueryResult::Fields& field_info);

        mxq::MariaDB m_conn;
        int64_t      last_max_rows {0};
        int64_t      last_timeout {0};
    };

    class ODBCConnection : public Connection
    {
    public:
        ODBCConnection(mxq::ODBC&& odbc, const ConnectionConfig& cnf);

        std::string error() override final;
        bool        cmd(const std::string& cmd) override final;
        uint32_t    thread_id() const override final;
        bool        reconnect() override final;
        bool        still_alive() override final;

    protected:
        mxb::Json do_query(const std::string& sql, int64_t max_rows, int64_t timeout) override final;
        void      do_cancel() override final;

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
     *         returned. If the connection was found, the connection info is also returned.
     */
    std::tuple<Connection*, Reason, Connection::Info> get_connection(const std::string& id);

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

    /**
     * Cancels an ongoing query
     *
     * @param id The connection ID
     *
     * @return True if the connection was found
     */
    bool cancel(const std::string& id);

    bool is_query(const std::string& conn_id, int64_t query_id) const;
    bool is_connection(const std::string& conn_id) const;

    std::vector<std::string> get_connections();

    json_t* connection_to_json(const std::string& conn_id);

    void start_cleanup_thread();
    void stop_cleanup_thread();
    void cancel_all_connections();

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
