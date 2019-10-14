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

#include <maxsql/mariadb_connector.hh>

#include <mysql.h>
#include <maxbase/assert.h>
#include <maxbase/format.hh>
#include <maxsql/mariadb.hh>

using std::string;
using mxb::string_printf;

namespace
{
const char no_connection[] = "MySQL-connection is not open, cannot perform query.";
}

namespace maxsql
{

MariaDB::~MariaDB()
{
    mysql_close(m_conn);
}

bool MariaDB::open(const std::string& host, unsigned int port, const std::string& db)
{
    mysql_close(m_conn);
    m_conn = nullptr;

    auto newconn = mysql_init(nullptr);
    if (!newconn)
    {
        m_errormsg = "Failed to allocate memory for MYSQL-handle.";
        m_errornum = INTERNAL_ERROR;
        return false;
    }

    bool rval = false;
    // TODO: add all possible settings like timeouts, ssl etc.
    if (mysql_real_connect(newconn, host.c_str(),
                           m_settings.user.c_str(), m_settings.password.c_str(), db.c_str(),
                           port, nullptr, 0) != nullptr)
    {
        clear_errors();
        m_conn = newconn;
        rval = true;
    }
    else
    {
        m_errormsg = (string)"Connector-C error: " + mysql_error(newconn);
        m_errornum = mysql_errno(newconn);
        mysql_close(newconn);
    }
    return rval;
}

const char* MariaDB::error() const
{
    return m_errormsg.c_str();
}

bool MariaDB::cmd(const std::string& sql)
{
    bool rval = false;
    if (m_conn)
    {
        bool query_success = (maxsql::mysql_query_ex(m_conn, sql, 0, 0) == 0);
        if (query_success)
        {
            MYSQL_RES* result = mysql_store_result(m_conn);
            if (!result)
            {
                // No data, as was expected.
                rval = true;
                clear_errors();
            }
            else
            {
                unsigned long cols = mysql_num_fields(result);
                unsigned long rows = mysql_num_rows(result);
                m_errormsg = string_printf(
                    "Query '%s' returned %lu columns and %lu rows of data when none was expected.",
                    sql.c_str(), cols, rows);
                m_errornum = USER_ERROR;
            }
        }
        else
        {
            m_errormsg = string_printf("Query '%s' failed: %s.", sql.c_str(), mysql_error(m_conn));
            m_errornum = mysql_errno(m_conn);
        }
    }
    else
    {
        m_errormsg = no_connection;
        m_errornum = USER_ERROR;
    }

    return rval;
}

std::unique_ptr<mxq::QueryResult> MariaDB::query(const std::string& query)
{
    using mxq::QueryResult;
    std::unique_ptr<QueryResult> rval;
    if (m_conn)
    {
        if (mysql_query(m_conn, query.c_str()) == 0)
        {
            MYSQL_RES* result = mysql_store_result(m_conn);
            if (result)
            {
                rval = std::unique_ptr<QueryResult>(new mxq::MariaDBQueryResult(result));
                clear_errors();
            }
            else
            {
                m_errormsg = mxb::string_printf("Query '%s' did not return any data.", query.c_str());
                m_errornum = USER_ERROR;
            }
        }
        else
        {
            m_errormsg = mxb::string_printf("Query '%s' failed: %s.", query.c_str(), mysql_error(m_conn));
            m_errornum = mysql_errno(m_conn);
        }
    }
    else
    {
        m_errormsg = no_connection;
        m_errornum = USER_ERROR;
    }

    return rval;
}

void MariaDB::clear_errors()
{
    m_errormsg.clear();
    m_errornum = 0;
}

void MariaDB::set_connection_settings(const MariaDB::ConnectionSettings& sett)
{
    m_settings = sett;
}

MariaDBQueryResult::MariaDBQueryResult(MYSQL_RES* resultset)
    : QueryResult(column_names(resultset))
    , m_resultset(resultset)
{
}

MariaDBQueryResult::~MariaDBQueryResult()
{
    mxb_assert(m_resultset);
    mysql_free_result(m_resultset);
}

bool MariaDBQueryResult::advance_row()
{
    m_rowdata = mysql_fetch_row(m_resultset);
    return m_rowdata;
}

int64_t MariaDBQueryResult::get_col_count() const
{
    return mysql_num_fields(m_resultset);
}

int64_t MariaDBQueryResult::get_row_count() const
{
    return mysql_num_rows(m_resultset);
}

const char* MariaDBQueryResult::row_elem(int64_t column_ind) const
{
    return m_rowdata[column_ind];
}

std::vector<std::string> MariaDBQueryResult::column_names(MYSQL_RES* resultset) const
{
    std::vector<std::string> rval;
    auto columns = mysql_num_fields(resultset);
    MYSQL_FIELD* field_info = mysql_fetch_fields(resultset);
    for (int64_t column_index = 0; column_index < columns; column_index++)
    {
        rval.emplace_back(field_info[column_index].name);
    }
    return rval;
}

}
