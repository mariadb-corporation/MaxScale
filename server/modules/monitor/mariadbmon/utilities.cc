/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "utilities.hh"

#include <inttypes.h>
#include <limits>
#include <string>
#include <sstream>
#include <maxscale/debug.h>
#include <maxscale/mysql_utils.h>

/** Server id default value */
const int64_t SERVER_ID_UNKNOWN = -1;

int64_t scan_server_id(const char* id_string)
{
    int64_t server_id = SERVER_ID_UNKNOWN;
    ss_debug(int rv = ) sscanf(id_string, "%" PRId64, &server_id);
    ss_dassert(rv == 1);
    // Server id can be 0, which was even the default value until 10.2.1.
    // KB is a bit hazy on this, but apparently when replicating, the server id should not be 0. Not sure,
    // so MaxScale allows this.
#if defined(SS_DEBUG)
    const int64_t SERVER_ID_MIN = std::numeric_limits<uint32_t>::min();
    const int64_t SERVER_ID_MAX = std::numeric_limits<uint32_t>::max();
#endif
    ss_dassert(server_id >= SERVER_ID_MIN && server_id <= SERVER_ID_MAX);
    return server_id;
}

bool query_one_row(MXS_MONITORED_SERVER *database, const char* query, unsigned int expected_cols,
                   StringVector* output)
{
    bool rval = false;
    MYSQL_RES *result;
    if (mxs_mysql_query(database->con, query) == 0 && (result = mysql_store_result(database->con)) != NULL)
    {
        unsigned int columns = mysql_field_count(database->con);
        if (columns != expected_cols)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for '%s'. Expected %d columns, got %d. Server version: %s",
                      query, expected_cols, columns, database->server->version_string);
        }
        else
        {
            MYSQL_ROW row = mysql_fetch_row(result);
            if (row)
            {
                for (unsigned int i = 0; i < columns; i++)
                {
                    output->push_back((row[i] != NULL) ? row[i] : "");
                }
                rval = true;
            }
            else
            {
                MXS_ERROR("Query '%s' returned no rows.", query);
            }
            mysql_free_result(result);
        }
    }
    else
    {
        mon_report_query_error(database);
    }
    return rval;
}

string get_connection_errors(const ServerVector& servers)
{
    // Get errors from all connections, form a string.
    std::stringstream ss;
    for (ServerVector::const_iterator iter = servers.begin(); iter != servers.end(); iter++)
    {
        const char* error = mysql_error((*iter)->con);
        ss_dassert(*error); // Every connection should have an error.
        ss << (*iter)->server->unique_name << ": '" << error << "'";
        if (iter + 1 != servers.end())
        {
            ss << ", ";
        }
    }
    return ss.str();
}

string monitored_servers_to_string(const ServerVector& array)
{
    string rval;
    size_t array_size = array.size();
    if (array_size > 0)
    {
        const char* separator = "";
        for (size_t i = 0; i < array_size; i++)
        {
            rval += separator;
            rval += array[i]->server->unique_name;
            separator = ",";
        }
    }
    return rval;
}

QueryResult::QueryResult(MYSQL_RES* resultset)
    : m_resultset(resultset)
    , m_columns(-1)
    , m_rowdata(NULL)
    , m_current_row(-1)
{
    if (m_resultset)
    {
        m_columns = mysql_num_fields(m_resultset);
        MYSQL_FIELD* field_info = mysql_fetch_fields(m_resultset);
        for (int64_t column_index = 0; column_index < m_columns; column_index++)
        {
            string key(field_info[column_index].name);
            // TODO: Think of a way to handle duplicate names nicely. Currently this should only be used
            // for known queries.
            ss_dassert(m_col_indexes.count(key) == 0);
            m_col_indexes[key] = column_index;
        }
    }
}

QueryResult::~QueryResult()
{
    if (m_resultset)
    {
        mysql_free_result(m_resultset);
    }
}

bool QueryResult::next_row()
{
    m_rowdata = mysql_fetch_row(m_resultset);
    if (m_rowdata != NULL)
    {
        m_current_row++;
        return true;
    }
    return false;
}

int64_t QueryResult::get_row_index() const
{
    return m_current_row;
}

int64_t QueryResult::get_column_count() const
{
    return m_columns;
}

int64_t QueryResult::get_col_index(const string& col_name) const
{
    auto iter = m_col_indexes.find(col_name);
    return (iter != m_col_indexes.end()) ? iter->second : -1;
}

string QueryResult::get_string(int64_t column_ind) const
{
    ss_dassert(column_ind < m_columns);
    char* data = m_rowdata[column_ind];
    return data ? data : "";
}

int64_t QueryResult::get_uint(int64_t column_ind) const
{
    ss_dassert(column_ind < m_columns);
    char* data = m_rowdata[column_ind];
    int64_t rval = -1;
    if (data)
    {
        errno = 0; // strtoll sets this
        auto parsed = strtoll(data, NULL, 10);
        if (parsed >= 0 && errno == 0)
        {
            rval = parsed;
        }
    }
    return rval;
}

bool QueryResult::get_bool(int64_t column_ind) const
{
    ss_dassert(column_ind < m_columns);
    char* data = m_rowdata[column_ind];
    return data ? (strcmp(data,"Y") == 0 || strcmp(data, "1") == 0) : false;
}
