/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxsql/ccdefs.hh>
#include <memory>
#include <string>
#include <maxsql/queryresult.hh>

struct sqlite3;
struct sqlite3_stmt;

namespace maxsql
{

class SQLiteStmt;

/**
 * Convenience class for working with SQLite.
 */
class SQLite
{
public:
    SQLite() = default;
    ~SQLite();
    SQLite(const SQLite& rhs) = delete;
    SQLite& operator=(const SQLite& rhs) = delete;

    static constexpr unsigned int INTERNAL_ERROR = 1;
    static constexpr unsigned int USER_ERROR = 2;

    template<class T>
    using Callback = int (*)(T* data, int n_columns, char** rows, char** field_names);

    /**
     * Create a new database handle.
     *
     * @param filename The filename/url given to sqlite3_open_v2
     * @param flags Flags given to sqlite3_open_v2
     * @return New handle if successful, null otherwise.
     */
    static std::unique_ptr<SQLite> create(const std::string& filename, int flags, std::string* error_out);

    /**
     * Open a new database handle.
     *
     * @param filename The filename/url given to sqlite3_open_v2
     * @param flags Flags given to sqlite3_open_v2
     * @return True on success
     */
    bool open(const std::string& filename, int flags);

    /**
     * Convenience function for opening a new private in-memory database.
     *
     * @return True on success
     */
    bool open_inmemory();

    /**
     * Run a simple query which returns no data.
     *
     * @param sql SQL to run
     * @return True on success
     */
    bool exec(const std::string& sql);

    /**
     * Run a query which may return data.
     *
     * @param sql SQL to run
     * @param cb Callback given to sqlite3_exec
     * @param cb_data Data pointer given to sqlite3_exec
     * @return True on success
     */
    template<class T>
    bool exec(const std::string& sql, Callback<T> cb, T* cb_data)
    {
        return exec_impl(sql, reinterpret_cast<CallbackVoid>(cb), cb_data);
    }

    /**
     * Calls sqlite3_busy_timeout.
     *
     * @param ms The timeout in ms
     */
    void set_timeout(int ms);

    /**
     * Get latest error.
     *
     * @return Error string
     */
    const char* error() const;

    /**
     * Run a query which may return data.
     *
     * @param query SQL to run
     * @return Query results on success, null otherwise
     */
    std::unique_ptr<mxq::QueryResult> query(const std::string& query);

    /**
     * Prepare a query.
     *
     * @param query Query string
     * @return The prepared statement object or null on failure
     */
    std::unique_ptr<SQLiteStmt> prepare(const std::string& query);

private:
    using CallbackVoid = int (*)(void* data, int n_columns, char** rows, char** field_names);
    bool exec_impl(const std::string& sql, CallbackVoid cb, void* cb_data);

    sqlite3*    m_dbhandle {nullptr};
    std::string m_errormsg;
    int         m_errornum {0};
};

/**
 * Class which represents an sqlite prepared statement.
 */
class SQLiteStmt
{
public:
    SQLiteStmt(sqlite3_stmt* stmt);
    ~SQLiteStmt();

    /**
     * Execute/get next result row of the prepared statement.
     *
     * @return True, if the new row exists.
     */
    bool step();

    bool step_execute();

    int column_count() const;

    /**
     * Resets the prepared statement. Calling step() runs the query again and may return updated results.
     *
     * @return True on success. If false, the statement should be discarded.
     */
    bool reset();

    int bind_parameter_index(const std::string& name);

    /**
     * Bind a string value to a parameter placeholder.
     *
     * @param ind Parameter index. Valid values start from 1.
     * @param value Parameter value
     */
    bool bind_string(int ind, const std::string& value);

    /**
     * Bind an integer value to a parameter placeholder.
     *
     * @param name Parameter name
     * @param ind Parameter index. Valid values start from 1.
     * @return
     */
    bool bind_int(int ind, int value);

    bool bind_bool(int ind, bool value);

    std::vector<std::string> column_names() const;

    /**
     * Fetch a result row as utf8 string.
     *
     * @param output Output array where the results will be written to. The array should be large enough
     * to fit every column.
     */
    void row_cstr(const unsigned char* output[]);

    /**
     * Get latest error number.
     *
     * @return Error number
     */
    int error() const;

private:
    sqlite3_stmt* m_stmt {nullptr};
    int           m_errornum {0};
};

/**
 * QueryResult implementation for SQLite.
 */
class SQLiteQueryResult : public QueryResult
{
public:
    /**
     * Construct a new resultset.
     *
     * @param resultset The results from mysql_query(). Must not be NULL.
     */
    explicit SQLiteQueryResult(SQLiteStmt& stmt);

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

    std::vector<std::string> m_column_names;    /**< Column names */
    std::vector<std::string> m_data_array;      /**< A flattened 2D-array with results */

    int m_cols {0};
    int m_rows {0};
    int m_current_row {0};
};
}
