/*
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxpgsql/ccdefs.hh>

#include <memory>
#include <string>
#include <maxbase/ssl.hh>

struct pg_conn;

namespace maxpgsql
{
/**
 * Convenience class for working with Pg connections.
 */
class PgSQL final
{
public:
    PgSQL() = default;
    virtual ~PgSQL();
    PgSQL(const PgSQL& rhs) = delete;
    PgSQL& operator=(const PgSQL& rhs) = delete;

    PgSQL(PgSQL&& conn) noexcept;
    PgSQL& operator=(PgSQL&& rhs) noexcept;

    struct ConnectionSettings
    {
        std::string user;
        std::string password;

        int            timeout {0};
        mxb::SSLConfig ssl;
    };
    /**
     * Open a new database connection.
     *
     * @param host Server host/ip
     * @param port Server port
     * @param db Database to connect to
     * @return True on success
     */
    bool open(const std::string& host, int port, const std::string& db = "");

    /**
     * Close connection.
     */
    void close();

    /**
     * Ping the server.
     * @return True on success
     */
    bool ping();

    bool        is_open() const;
    const char* error() const;

    ConnectionSettings& connection_settings();

private:
    pg_conn*           m_conn {nullptr};
    ConnectionSettings m_settings;

    void move_helper(PgSQL&& other);
};
}
