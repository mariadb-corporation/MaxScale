/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <mysql.h>
#include <mariadb_rpl.h>

#include <memory>
#include <vector>

#include "config.hh"

// Convenience class that wraps a MYSQL connection and provides a minimal C++ interface
class SQL
{
public:
    SQL(const SQL&) = delete;
    SQL& operator=(const SQL&) = delete;

    using Row = std::vector<std::string>;
    using Result = std::vector<Row>;
    using Event = std::unique_ptr<MARIADB_RPL_EVENT, std::function<decltype(mariadb_free_rpl_event)>>;

    /**
     * Create a new connection from a list of servers
     *
     * The first available server is chosen from the provided list
     *
     * @param servers         List of server candidates
     * @param connect_timeout Connect timeout in seconds, defaults to 10 seconds
     * @param read_timeout    Read timeout in seconds, defaults to 5 seconds
     *
     * @return The error message and a unique_ptr. If an error occurred, the error string contains the
     *         error description and the unique_ptr is empty.
     */
    static std::pair<std::string, std::unique_ptr<SQL>> connect(const std::vector<cdc::Server>& servers,
                                                                int connect_timeout = 30,
                                                                int read_timeout = 60);

    ~SQL();

    /**
     * Execute a query
     *
     * @param sql SQL to execute
     *
     * @return True on success, false on error
     */
    bool query(const std::string& sql);
    bool query(const std::vector<std::string>& sql);

    /**
     * Return latest error string
     *
     * @return The latest error
     */
    std::string error() const
    {
        return mysql_error(m_mysql);
    }

    /**
     * Return latest error number
     *
     * @return The latest number
     */
    int errnum() const
    {
        return mysql_errno(m_mysql);
    }

    /**
     * Return the server where the connection was created
     *
     * @return The server where the connection was created
     */
    const cdc::Server& server() const
    {
        return m_server;
    }

    /**
     * Start replicating data from the server
     *
     * @param server_id Server ID to connect with
     *
     * @return True if replication was started successfully
     */
    bool replicate(int server_id);

    /**
     * Fetch one replication event
     *
     * @return The next replicated event or null on error
     */
    Event fetch_event()
    {
        return Event {mariadb_rpl_fetch(m_rpl, nullptr), mariadb_free_rpl_event};
    }

    /**
     * Pointer to the raw event data
     */
    uint8_t* event_data() const
    {
        return m_rpl->buffer;
    }


    Result result(const std::string& sql);

private:
    SQL(MYSQL* mysql, const cdc::Server& server);

    MYSQL*       m_mysql {nullptr};     // Database handle
    MARIADB_RPL* m_rpl {nullptr};       // Replication handle
    cdc::Server  m_server;              // The server where the connection was made
};

// String conversion helper
static inline std::string to_string(const MARIADB_STRING& str)
{
    return std::string(str.str, str.length);
}
