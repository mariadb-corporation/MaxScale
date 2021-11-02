/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
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

namespace
{
const char no_handle[] = "SQLite-handle is not open, cannot perform query.";
}

namespace maxsql
{

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

    sqlite3_close(m_dbhandle);   // Close any existing handle.
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
        sqlite3_close(new_handle);
    }
    else
    {
        m_errormsg = mxb::string_printf(open_oom, zFilename);
    }

    return success;
}

bool SQLite::open_inmemory()
{
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    return open(":memory:", flags);
}

SQLite::~SQLite()
{
    sqlite3_close(m_dbhandle);
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

SQLiteStmt::SQLiteStmt(sqlite3_stmt* stmt)
    : m_stmt(stmt)
{
}

SQLiteStmt::~SQLiteStmt()
{
    sqlite3_finalize(m_stmt);
}

bool SQLiteStmt::step()
{
    int ret = sqlite3_step(m_stmt);
    m_errornum = ret;
    return ret == SQLITE_ROW;
}

bool SQLiteStmt::step_execute()
{
    int ret = sqlite3_step(m_stmt);
    m_errornum = ret;
    return ret == SQLITE_DONE;
}


bool SQLiteStmt::reset()
{
    int ret = sqlite3_reset(m_stmt);
    if (ret == SQLITE_OK)
    {
        ret = sqlite3_clear_bindings(m_stmt);
    }
    m_errornum = ret;
    return ret == SQLITE_OK;
}

int SQLiteStmt::column_count() const
{
    return sqlite3_column_count(m_stmt);
}

void SQLiteStmt::row_cstr(const unsigned char* output[])
{
    int cols = column_count();
    for (int i = 0; i < cols; i++)
    {
        output[i] = sqlite3_column_text(m_stmt, i);
    }
}

std::vector<std::string> SQLiteStmt::column_names() const
{
    int cols = column_count();
    std::vector<std::string> rval;
    rval.reserve(cols);
    for (int i = 0; i < cols; i++)
    {
        rval.emplace_back(sqlite3_column_name(m_stmt, i));
    }
    return rval;
}

int SQLiteStmt::bind_parameter_index(const std::string& name)
{
    // Parameter names must start with ":" or "@". Add ":" if none found.
    int rval = 0; // Valid indexes start at 1.
    if (!name.empty())
    {
        auto front = name.front();
        if (front != ':' && front != '@')
        {
            string fixedname = ":" + name;
            rval = sqlite3_bind_parameter_index(m_stmt, fixedname.c_str());
        }
        else
        {
            rval = sqlite3_bind_parameter_index(m_stmt, name.c_str());
        }
    }
    return rval;
}

bool SQLiteStmt::bind_string(int ind, const string& value)
{
    int ret = sqlite3_bind_text(m_stmt, ind, value.c_str(), value.size(), nullptr);
    m_errornum = ret;
    return ret == SQLITE_OK;
}

bool SQLiteStmt::bind_int(int ind, int value)
{
    int ret = sqlite3_bind_int(m_stmt, ind, value);
    m_errornum = ret;
    return ret == SQLITE_OK;
}

bool SQLiteStmt::bind_bool(int ind, bool value)
{
    int ret = sqlite3_bind_int(m_stmt, ind, value ? 1 : 0);
    m_errornum = ret;
    return ret == SQLITE_OK;
}

int SQLiteStmt::error() const
{
    return m_errornum;
}

std::unique_ptr<mxq::QueryResult> SQLite::query(const std::string& query)
{
    using mxq::QueryResult;
    std::unique_ptr<QueryResult> rval;
    if (m_dbhandle)
    {
        auto stmt = prepare(query);
        if (stmt)
        {
            if (stmt->column_count() > 0)
            {
                rval.reset(new(std::nothrow) mxq::SQLiteQueryResult(*stmt));
            }
            else
            {
                m_errormsg = mxb::string_printf("Query '%s' did not return any data.", query.c_str());
                m_errornum = USER_ERROR;
            }
        }
    }
    else
    {
        m_errormsg = no_handle;
        m_errornum = USER_ERROR;
    }

    return rval;
}

std::unique_ptr<SQLiteStmt> SQLite::prepare(const std::string& query)
{
    std::unique_ptr<SQLiteStmt> rval;
    sqlite3_stmt* new_stmt = nullptr;
    const char* tail = nullptr;
    const char* queryz = query.c_str();

    int ret = sqlite3_prepare_v2(m_dbhandle, queryz, query.length() + 1, &new_stmt, &tail);
    if (ret == SQLITE_OK)
    {
        // Compilation succeeded.
        if (!*tail)
        {
            rval.reset(new(std::nothrow) SQLiteStmt(new_stmt));
            new_stmt = nullptr;
            m_errormsg.clear();
            m_errornum = 0;
        }
        else
        {
            m_errormsg = mxb::string_printf("Query '%s' contained multiple statements. Only singular "
                                            "statements are supported.", queryz);
            m_errornum = USER_ERROR;
            sqlite3_finalize(new_stmt);
        }
    }
    else
    {
        m_errormsg = mxb::string_printf("Could not prepare query '%s': %s",
                                        queryz, sqlite3_errmsg(m_dbhandle));
        m_errornum = ret;
    }

    return rval;
}

SQLiteQueryResult::SQLiteQueryResult(SQLiteStmt& stmt)
    : QueryResult(stmt.column_names())
{
    m_column_names = stmt.column_names();
    int cols = stmt.column_count();
    mxb_assert(m_column_names.size() == (size_t)cols);
    m_cols = cols;

    while (stmt.step())
    {
        m_rows++;
        const unsigned char* row_elems[cols];
        stmt.row_cstr(row_elems);
        for (int i = 0; i < cols; i++)
        {
            const char* elem = reinterpret_cast<const char*>(row_elems[i]);
            m_data_array.emplace_back(elem ? elem : "");
        }
    }
}

bool SQLiteQueryResult::advance_row()
{
    m_current_row++;
    return m_current_row < m_rows;
}

int64_t SQLiteQueryResult::get_col_count() const
{
    return m_cols;
}

int64_t SQLiteQueryResult::get_row_count() const
{
    return m_rows;
}

const char* SQLiteQueryResult::row_elem(int64_t column_ind) const
{
    int64_t coord = m_current_row * m_cols + column_ind;
    return m_data_array[coord].c_str();
}

}
