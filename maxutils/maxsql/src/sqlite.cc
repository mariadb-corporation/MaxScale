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

#include <maxsql/sqlite.hh>
#include <sqlite3.h>
#include <maxbase/assert.h>
#include <maxbase/format.hh>

using std::string;

std::unique_ptr<SQLite> SQLite::create(const string& filename, int flags, string* error_out)
{
    std::unique_ptr<SQLite> new_handle(new SQLite());
    if (new_handle && new_handle->open(filename, flags))
    {
        return new_handle;
    }
    return nullptr;
}

bool SQLite::open(const std::string& filename, int flags)
{
    const char open_fail[] = "Failed to open SQLite3 handle for file '%s': '%s'";
    const char open_oom[] = "Failed to allocate memory for SQLite3 handle for file '%s'.";

    sqlite3_close_v2(m_dbhandle); // Close any existing handle.
    m_dbhandle = nullptr;
    m_errormsg.clear();

    sqlite3* new_handle = nullptr;
    const char* zFilename = filename.c_str();
    string error_msg;
    bool success = false;
    if (sqlite3_open_v2(zFilename, &new_handle, flags, NULL) == SQLITE_OK)
    {
        m_dbhandle = new_handle;
        success = true;
    }
    // Even if the open failed, the handle may exist and an error message can be read.
    else if (new_handle)
    {
        m_errormsg = mxb::string_printf(open_fail, zFilename, sqlite3_errmsg(new_handle));
        sqlite3_close_v2(new_handle);
    }
    else
    {
        m_errormsg = mxb::string_printf(open_oom, zFilename);
    }

    return success;
}

SQLite::~SQLite()
{
    sqlite3_close_v2(m_dbhandle);
}

bool SQLite::exec(const std::string& sql)
{
    return exec_impl(sql, nullptr, nullptr);
}

bool SQLite::exec_impl(const std::string& sql, CallbackVoid cb, void* cb_data)
{
    char* err = nullptr;
    bool success = (sqlite3_exec(m_dbhandle, sql.c_str(), cb, cb_data, &err) == SQLITE_OK);
    if (success)
    {
        m_errormsg.clear();
    }
    else
    {
        m_errormsg = err;
        sqlite3_free(err);
    }
    return success;
}

void SQLite::set_timeout(int ms)
{
    sqlite3_busy_timeout(m_dbhandle, ms);
}

const char* SQLite::error() const
{
    return m_errormsg.c_str();
}
