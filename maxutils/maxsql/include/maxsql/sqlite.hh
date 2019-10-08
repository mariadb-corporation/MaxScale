/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxsql/ccdefs.hh>
#include <memory>
#include <string>

struct sqlite3;

class SQLite;

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
    bool open(const std::string& filename, int flags, std::string* error_out);

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
    template <class T>
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

private:
    using CallbackVoid = int (*)(void* data, int n_columns, char** rows, char** field_names);
    bool exec_impl(const std::string& sql, CallbackVoid cb, void* cb_data);

    sqlite3* m_dbhandle {nullptr};
    std::string m_errormsg;
};
