/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxsql/mariadb.hh>
#include <time.h>
#include <errmsg.h>
#include <string>
#include <string.h>
#include <maxbase/assert.h>

using std::string;

namespace
{
struct THIS_UNIT
{
    bool log_statements;    // Should all statements sent to server be logged?
};

static THIS_UNIT this_unit =
{
    false
};
}

namespace maxsql
{

int mysql_query_ex(MYSQL* conn, const std::string& query, int query_retries, time_t query_retry_timeout)
{
    const char* query_cstr = query.c_str();
    time_t start = time(NULL);
    int rc = mysql_query(conn, query_cstr);

    for (int n = 0; rc != 0 && n < query_retries && mysql_is_net_error(mysql_errno(conn))
         && time(NULL) - start < query_retry_timeout; n++)
    {
        rc = mysql_query(conn, query_cstr);
    }

    if (this_unit.log_statements)
    {
        const char* host = "0.0.0.0";
        unsigned int port = 0;
        MXB_AT_DEBUG(int rc1 = ) mariadb_get_info(conn, MARIADB_CONNECTION_HOST, &host);
        MXB_AT_DEBUG(int rc2 = ) mariadb_get_info(conn, MARIADB_CONNECTION_PORT, &port);
        mxb_assert(!rc1 && !rc2);
        MXB_NOTICE("SQL([%s]:%u): %d, \"%s\"", host, port, rc, query_cstr);
    }

    return rc;
}

bool mysql_is_net_error(unsigned int errcode)
{
    switch (errcode)
    {
    case CR_SOCKET_CREATE_ERROR:
    case CR_CONNECTION_ERROR:
    case CR_CONN_HOST_ERROR:
    case CR_IPSOCK_ERROR:
    case CR_SERVER_GONE_ERROR:
    case CR_TCP_CONNECTION:
    case CR_SERVER_LOST:
        return true;

    default:
        return false;
    }
}

void mysql_set_log_statements(bool enable)
{
    this_unit.log_statements = enable;
}

bool mysql_get_log_statements()
{
    return this_unit.log_statements;
}

QueryResult::QueryResult(MYSQL_RES* resultset)
    : m_resultset(resultset)
{
    mxb_assert(m_resultset);
    auto columns = mysql_num_fields(m_resultset);
    MYSQL_FIELD* field_info = mysql_fetch_fields(m_resultset);
    for (int64_t column_index = 0; column_index < columns; column_index++)
    {
        string key(field_info[column_index].name);
        // TODO: Think of a way to handle duplicate names nicely. Currently this should only be used
        // for known queries.
        mxb_assert(m_col_indexes.count(key) == 0);
        m_col_indexes[key] = column_index;
    }
}

QueryResult::~QueryResult()
{
    mxb_assert(m_resultset);
    mysql_free_result(m_resultset);
}

bool QueryResult::next_row()
{
    m_rowdata = mysql_fetch_row(m_resultset);
    if (m_rowdata)
    {
        m_current_row_ind++;
        return true;
    }
    else
    {
        m_current_row_ind = -1;
        return false;
    }
}

int64_t QueryResult::get_current_row_index() const
{
    return m_current_row_ind;
}

int64_t QueryResult::get_col_count() const
{
    return mysql_num_fields(m_resultset);
}

int64_t QueryResult::get_row_count() const
{
    return mysql_num_rows(m_resultset);
}

int64_t QueryResult::get_col_index(const string& col_name) const
{
    auto iter = m_col_indexes.find(col_name);
    return (iter != m_col_indexes.end()) ? iter->second : -1;
}

string QueryResult::get_string(int64_t column_ind) const
{
    mxb_assert(column_ind < get_col_count() && column_ind >= 0 && m_rowdata);
    char* data = m_rowdata[column_ind];
    return data ? data : "";
}

int64_t QueryResult::get_uint(int64_t column_ind) const
{
    mxb_assert(column_ind < get_col_count() && column_ind >= 0 && m_rowdata);
    char* data = m_rowdata[column_ind];
    int64_t rval = -1;
    if (data && *data)
    {
        errno = 0;      // strtoll sets this
        char* endptr = NULL;
        auto parsed = strtoll(data, &endptr, 10);
        if (parsed >= 0 && errno == 0 && *endptr == '\0')
        {
            rval = parsed;
        }
    }
    return rval;
}

bool QueryResult::get_bool(int64_t column_ind) const
{
    mxb_assert(column_ind < get_col_count() && column_ind >= 0 && m_rowdata);
    char* data = m_rowdata[column_ind];
    return data ? (strcmp(data, "Y") == 0 || strcmp(data, "1") == 0) : false;
}

}
