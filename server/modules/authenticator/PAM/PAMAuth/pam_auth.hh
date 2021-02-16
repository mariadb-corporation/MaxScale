/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/*
 * Common definitions and includes for PAM client authenticator
 */
#define MXS_MODULE_NAME "PAMAuth"

#include <maxscale/ccdefs.hh>

#include <string>
#include <maxbase/alloc.h>
#include <maxscale/buffer.hh>
#include <maxscale/dcb.hh>
#include <maxscale/protocol/mysql.hh>

using std::string;

extern const string FIELD_USER;
extern const string FIELD_HOST;
extern const string FIELD_DB;
extern const string FIELD_ANYDB;
extern const string FIELD_AUTHSTR;
extern const string FIELD_DEF_ROLE;
extern const string FIELD_HAS_PROXY;
extern const string FIELD_IS_ROLE;
extern const string FIELD_ROLE;

extern const char* SQLITE_OPEN_FAIL;
extern const char* SQLITE_OPEN_OOM;

extern const string TABLE_USER;
extern const string TABLE_DB;
extern const string TABLE_ROLES_MAPPING;

struct sqlite3;

class SQLite;

/**
 * Convenience class for working with SQLite.
 */
class SQLite
{
public:
    SQLite(const SQLite& rhs) = delete;
    SQLite& operator=(const SQLite& rhs) = delete;

    using SSQLite = std::unique_ptr<SQLite>;

    template<class T>
    using Callback = int (*)(T* data, int n_columns, char** rows, char** field_names);

    /**
     * Create a new database handle.
     *
     * @param filename The filename/url given to sqlite3_open_v2
     * @param flags Flags given to sqlite3_open_v2
     * @return New handle if successful, null otherwise.
     */
    static SSQLite create(const std::string& filename, int flags, std::string* error_out);
    ~SQLite();

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

    SQLite(sqlite3* handle);

    sqlite3* m_dbhandle {nullptr};
    std::string m_errormsg;
};
