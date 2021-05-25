/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxsql/ccdefs.hh>

#include <memory>
#include <string>
#include <maxbase/ssl.hh>
#include <maxsql/queryresult.hh>

struct st_mysql;
struct st_mysql_res;

namespace maxsql
{

/**
 * Convenience class for working with Connector-C.
 */
class MariaDB
{
public:
    MariaDB() = default;
    virtual ~MariaDB();
    MariaDB(const MariaDB& rhs) = delete;
    MariaDB& operator=(const MariaDB& rhs) = delete;

    struct ConnectionSettings
    {
        std::string user;
        std::string password;

        std::string local_address;
        std::string plugin_dir;

        mxb::SSLConfig ssl;
        std::string    ssl_version;

        int  timeout {0};
        bool multiquery {false};
        bool auto_reconnect {false};

        bool        clear_sql_mode {false};
        std::string charset;
        // TODO: add more
    };

    struct VersionInfo
    {
        uint64_t    version {0};
        std::string info;
    };

    static constexpr unsigned int INTERNAL_ERROR = 1;
    static constexpr unsigned int USER_ERROR = 2;

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
     * Open, with extra port. If extra port is greater than 0 and connection with normal port failed due
     * to too many connections, then connecting using the extra port is attempted.
     *
     * @param host Server host/ip
     * @param port Server port
     * @param extra_port Extra port
     * @param db Database to connect to
     * @return True on success
     */
    bool open_extra(const std::string& host, int port, int extra_port, const std::string& db = "");

    /**
     * Closes any existing connection.
     */
    void close();

    /**
     * Run a query which returns no data.
     *
     * @param sql SQL to run
     * @return True on success
     */
    bool cmd(const std::string& sql);

    /**
     * Run a query which may return data.
     *
     * @param query SQL to run
     * @return Query results on success, null otherwise
     */
    std::unique_ptr<mxq::QueryResult> query(const std::string& query);

    /**
     * Run multiple queries as one. Each should return data. Multiqueries should be enabled in connection
     * settings.
     *
     * @param queries Array of queries. Will be executed as a single multiquery. Each individual query
     * should end in a semicolon.
     * @return Results from every query. If any kind of error occurs, returns an empty vector.
     */
    std::vector<std::unique_ptr<mxq::QueryResult>> multiquery(const std::vector<std::string>& queries);

    /**
     * Ping the server.
     *
     * @return True on success
     */
    bool ping();

    /**
     * Get latest error.
     *
     * @return Error string
     */
    const char* error() const;

    /**
     * Get latest error number.
     *
     * @return Error code
     */
    int64_t errornum() const;

    /**
     * Get reference to connection settings. The settings are used when opening a connection.
     *
     * @return Connection settings reference
     */
    ConnectionSettings& connection_settings();

    VersionInfo version_info() const;
    bool        is_open() const;

private:
    void clear_errors();

    st_mysql*   m_conn {nullptr};
    std::string m_errormsg;
    int64_t     m_errornum {0};

    ConnectionSettings m_settings;
};

/**
 * QueryResult implementation for the MariaDB-class.
 */
class MariaDBQueryResult : public QueryResult
{
public:
    MariaDBQueryResult(const MariaDBQueryResult&) = delete;
    MariaDBQueryResult& operator=(const MariaDBQueryResult&) = delete;

    /**
     * Construct a new resultset.
     *
     * @param resultset The results from mysql_query(). Must not be NULL.
     */
    explicit MariaDBQueryResult(st_mysql_res* resultset);

    ~MariaDBQueryResult() override;

    /**
     * Advance to next row. Affects all result returning functions.
     *
     * @return True if the next row has data, false if the current row was the last one.
     */


    /**
     * How many columns the result set has.
     *
     * @return Column count
     */
    int64_t get_col_count() const override;

    /**
     * How many rows does the result set have?
     *
     * @return The number of rows
     */
    int64_t get_row_count() const override;

private:
    const char* row_elem(int64_t column_ind) const override;
    bool        advance_row() override;

    static std::vector<std::string> column_names(st_mysql_res* results);

    st_mysql_res*      m_resultset {nullptr};   /**< Underlying result set, freed at dtor */
    const char* const* m_rowdata {nullptr};     /**< Data for current row */
};
}
