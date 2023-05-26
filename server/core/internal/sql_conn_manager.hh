/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
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

#include <maxbase/jansson.hh>
#include <maxbase/stopwatch.hh>
#include <maxsql/mariadb_connector.hh>

namespace HttpSql
{

struct ConnectionConfig
{
    std::string    host;
    int            port;
    std::string    user;
    std::string    password;
    std::string    db;
    int64_t        timeout = 10;
    bool           proxy_protocol = false;
    mxb::SSLConfig ssl;
};

class ConnectionManager
{
public:
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;
    ConnectionManager() = default;
    ~ConnectionManager();

    struct Connection
    {
        Connection(mxq::MariaDB&& new_conn, const ConnectionConfig& cnf);
        ~Connection();
        void release();

        json_t* to_json() const;

        std::atomic_bool busy {false};
        mxq::MariaDB     conn;
        int64_t          current_query_id {0};
        mxb::TimePoint   last_query_time;
        int64_t          last_max_rows {0};
        ConnectionConfig config;
    };

    /**
     * Get a connection by id and set the connection state to busy. Once the caller is done with
     * the connection, they should call 'release', allowing the connection
     * to be used again.
     *
     * @param id Connection id
     * @return Pointer to connection when id found and connection is not busy, null otherwise.
     */
    Connection* get_connection(const std::string& id);

    /**
     * Get the configuration of a connection
     *
     * @return The configuration of the given connection if one with the given ID exists
     */
    std::optional<ConnectionConfig> get_configuration(const std::string& id);

    /**
     * Add a connection to the map.
     *
     * @param conn Existing Connector-C connection
     * @return Id of added connection
     */
    std::string add(mxq::MariaDB&& conn, const ConnectionConfig& cnf);

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
