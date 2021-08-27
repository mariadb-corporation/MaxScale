/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
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

#include <maxbase/stopwatch.hh>
#include <maxsql/mariadb_connector.hh>

namespace HttpSql
{

class ConnectionManager
{
public:
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;
    ConnectionManager() = default;
    ~ConnectionManager();

    struct Connection
    {
        Connection(mxq::MariaDB&& new_conn);
        ~Connection();
        void release();

        std::atomic_bool busy {false};
        mxq::MariaDB     conn;
        int64_t          current_query_id {0};
        mxb::TimePoint   last_query_time;
    };

    /**
     * Get a connection by id and set the connection state to busy. Once the caller is done with
     * the connection, they should call 'release', allowing the connection
     * to be used again.
     *
     * @param id Connection id
     * @return Pointer to connection when id found and connection is not busy, null otherwise.
     */
    Connection* get_connection(int64_t id);

    /**
     * Add a connection to the map.
     *
     * @param conn Existing Connector-C connection
     * @return Id of added connection
     */
    int64_t add(mxq::MariaDB&& conn);

    /**
     * Erase a connection from the map.
     *
     * @param id Id of connection to erase
     * @return True if erased. False if id not found or was busy.
     */
    bool erase(int64_t id);

    bool is_query(int64_t conn_id, int64_t query_id) const;
    bool is_connection(int64_t conn_id) const;

    std::vector<int64_t> get_connections();

    void start_cleanup_thread();
    void stop_cleanup_thread();

private:
    mutable std::mutex m_connection_lock;   /**< Protects access to connections map and next id */

    std::map<int64_t, std::unique_ptr<Connection>> m_connections;   /**< Connections by id */
    int64_t                                        m_next_id {1};   /**< Id of next connection */

    // Fields for controlling the cleanup thread.
    std::atomic_bool        m_keep_running {true};
    std::condition_variable m_stop_running_notifier;
    std::mutex              m_notifier_lock;
    std::thread             m_cleanup_thread;

    void cleanup_thread_func();
};
}
