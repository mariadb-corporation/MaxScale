/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxsql/ccdefs.hh>

#include <memory>
#include <string>
#include <mysql.h>
#include <maxsql/queryresult.hh>

namespace maxsql
{

/**
 * Convenience class for working with Connector-C.
 */
class MariaDB
{
public:
    MariaDB() = default;
    ~MariaDB();
    MariaDB(const MariaDB& rhs) = delete;
    MariaDB& operator=(const MariaDB& rhs) = delete;

    struct ConnectionSettings
    {
        std::string user;
        std::string password;
        // TODO: add more
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
    bool open(const std::string& host, unsigned int port, const std::string& db = "");

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
     * Get latest error.
     *
     * @return Error string
     */
    const char* error() const;

    void set_connection_settings(const ConnectionSettings& sett);

private:
    void clear_errors();

    MYSQL*       m_conn {nullptr};
    std::string  m_errormsg;
    unsigned int m_errornum {0};

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
    MariaDBQueryResult(MYSQL_RES* resultset);

    ~MariaDBQueryResult();

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
    const char*              row_elem(int64_t column_ind) const override;
    bool                     advance_row() override;
    std::vector<std::string> column_names(MYSQL_RES* results) const;

    MYSQL_RES* m_resultset {nullptr};   /**< Underlying result set, freed at dtor */
    MYSQL_ROW  m_rowdata {nullptr};     /**< Data for current row */
};
}
