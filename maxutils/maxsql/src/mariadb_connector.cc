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

#include <memory>
#include <mysql.h>
#include <maxbase/assert.h>
#include <maxbase/format.hh>
#include <maxbase/string.hh>
#include <maxsql/mariadb.hh>

using std::string;
using std::vector;
using std::unique_ptr;
using mxb::string_printf;
using mxq::QueryResult;

namespace
{
const char no_connection[] = "MySQL-connection is not open, cannot perform query.";
const char query_failed[] = "Query '%s' failed: %s.";
const char no_data[] = "Query '%s' did not return any data.";
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
    if (!m_settings.ssl.empty())
    {
        // If an option is empty, a null-pointer should be given to mysql_ssl_set.
        const char* ssl_key = m_settings.ssl.key.empty() ? nullptr : m_settings.ssl.key.c_str();
        const char* ssl_cert = m_settings.ssl.cert.empty() ? nullptr : m_settings.ssl.cert.c_str();
        const char* ssl_ca = m_settings.ssl.ca.empty() ? nullptr : m_settings.ssl.ca.c_str();
        mysql_ssl_set(newconn, ssl_key, ssl_cert, ssl_ca, nullptr, nullptr);
    }

    if (m_settings.timeout > 0)
    {
        // Use the same timeout for all three settings for now.
        mysql_optionsv(newconn, MYSQL_OPT_CONNECT_TIMEOUT, &m_settings.timeout);
        mysql_optionsv(newconn, MYSQL_OPT_READ_TIMEOUT, &m_settings.timeout);
        mysql_optionsv(newconn, MYSQL_OPT_WRITE_TIMEOUT, &m_settings.timeout);
    }

    if (!m_settings.local_address.empty())
    {
        mysql_optionsv(newconn, MYSQL_OPT_BIND, m_settings.local_address.c_str());
    }
    if (m_settings.multiquery)
    {
        mysql_optionsv(newconn, MARIADB_OPT_MULTI_STATEMENTS, 1);
    }

    bool connection_success = false;
    const char* userz = m_settings.user.c_str();
    const char* passwdz = m_settings.password.c_str();
    const char* hostz = host.empty() ? nullptr : host.c_str();
    const char* dbz = db.c_str();

    if (hostz == nullptr || hostz[0] != '/')
    {
        // Assume the host is a normal address. Empty host is treated as "localhost".
        if (mysql_real_connect(newconn, hostz, userz, passwdz, dbz, port, nullptr, 0) != nullptr)
        {
            connection_success = true;
        }
    }
    else
    {
        // The host looks like an unix socket.
        if (mysql_real_connect(newconn, nullptr, userz, passwdz, dbz, 0, hostz, 0) != nullptr)
        {
            connection_success = true;
        }
    }

    if (connection_success && mysql_query(newconn, "SET SQL_MODE=''") == 0)
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
            m_errormsg = string_printf(query_failed, sql.c_str(), mysql_error(m_conn));
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
                m_errormsg = mxb::string_printf(no_data, query.c_str());
                m_errornum = USER_ERROR;
            }
        }
        else
        {
            m_errormsg = mxb::string_printf(query_failed, query.c_str(), mysql_error(m_conn));
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

MariaDB::ConnectionSettings& MariaDB::connection_settings()
{
    return m_settings;
}

vector<unique_ptr<QueryResult>> MariaDB::multiquery(const vector<string>& queries)
{
    vector<unique_ptr<QueryResult>> rval;
    if (m_conn)
    {
        string multiquery = mxb::create_list_string(queries, " ");
        if (mysql_query(m_conn, multiquery.c_str()) == 0)
        {
            auto n_queries = queries.size();
            vector<unique_ptr<QueryResult>> results;
            results.reserve(n_queries);

            // Fetch all resultsets...
            bool more_data = true;
            while (more_data)
            {
                std::unique_ptr<QueryResult> new_elem;
                MYSQL_RES* result = mysql_store_result(m_conn);
                if (result)
                {
                    new_elem = std::make_unique<mxq::MariaDBQueryResult>(result);
                }
                results.push_back(move(new_elem));
                more_data = (mysql_next_result(m_conn) == 0);
            }

            // and check they all returned results.
            string errormsg;
            int errornum = 0;
            for (size_t i = 0; i < results.size(); i++)
            {
                if (results[i] == nullptr)
                {
                    auto errored_query = (i < n_queries) ? queries[i].c_str() : "<unknown-query>";
                    errormsg = string_printf("Multiquery element '%s' did not return any data.",
                                             errored_query);
                    errornum = USER_ERROR;
                    break;
                }
            }

            if (errormsg.empty())
            {
                if (results.size() == n_queries)
                {
                    // success
                    clear_errors();
                    rval = move(results);
                }
                else
                {
                    // If received wrong number of results, return nothing.
                    m_errormsg = string_printf("Wrong number of resultsets to multiquery '%s'. Got %zi, "
                                               "expected %zi.",
                                               multiquery.c_str(), results.size(), n_queries);
                    m_errornum = USER_ERROR;
                }
            }
            else
            {
                m_errormsg = errormsg;
                m_errornum = errornum;
            }
        }
        else
        {
            m_errormsg = string_printf(query_failed, multiquery.c_str(), mysql_error(m_conn));
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

MariaDB::VersionInfo MariaDB::version_info() const
{
    const char* info = nullptr;
    unsigned long version = 0;
    if (m_conn)
    {
        info = mysql_get_server_info(m_conn);
        version = mysql_get_server_version(m_conn);
    }
    return VersionInfo {version, info ? info : ""};
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

std::vector<std::string> MariaDBQueryResult::column_names(MYSQL_RES* resultset)
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
