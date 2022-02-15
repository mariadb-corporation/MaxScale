/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxsql/mariadb_connector.hh>

#include <memory>
#include <mysql.h>
#include <mysqld_error.h>
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
const char no_connection[]       = "MySQL-connection is not open, cannot perform query.";
const char query_failed[]        = "Query '%s' failed. Error %li: %s.";
const char multiq_elem_failed[]  = "Multiquery element '%s' failed. Error %li: %s.";
const char no_data[]             = "Query '%s' did not return any results.";
const char multiq_elem_no_data[] = "Multiquery element '%s' did not return any results.";

static std::string default_plugin_dir = "/usr/lib/mysql/plugin/";
}  // namespace

namespace maxsql
{

// static
void MariaDB::set_default_plugin_dir(const std::string& dir)
{
    default_plugin_dir = dir;
}

MariaDB::~MariaDB()
{
    close();
}

bool MariaDB::open(const std::string& host, int port, const std::string& db)
{
    mxb_assert(port >= 0);  // MaxScale config loader should not accept negative values. 0 is ok.
    close();

    auto newconn = mysql_init(nullptr);
    if (!newconn)
    {
        m_errornum = INTERNAL_ERROR;
        m_errormsg = "Failed to allocate memory for MYSQL-handle.";
        return false;
    }

    // Set various connection options. Checking the return value of mysql_optionsv is pointless, as it
    // rarely checks the inputs. The errors are likely reported when connecting.
    if (m_settings.timeout > 0)
    {
        // Use the same timeout for all three settings.
        auto timeout = m_settings.timeout;
        mysql_optionsv(newconn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
        mysql_optionsv(newconn, MYSQL_OPT_READ_TIMEOUT, &timeout);
        mysql_optionsv(newconn, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
    }

    const std::string& dir = m_settings.plugin_dir.empty() ? default_plugin_dir : m_settings.plugin_dir;
    mysql_optionsv(newconn, MYSQL_PLUGIN_DIR, dir.c_str());

    bool ssl_enabled = false;
    if (!m_settings.ssl.empty())
    {
        // If an option is empty, a null-pointer should be given to mysql_ssl_set.
        auto& ssl            = m_settings.ssl;
        const char* ssl_key  = ssl.key.empty() ? nullptr : ssl.key.c_str();
        const char* ssl_cert = ssl.cert.empty() ? nullptr : ssl.cert.c_str();
        const char* ssl_ca   = ssl.ca.empty() ? nullptr : ssl.ca.c_str();
        mysql_ssl_set(newconn, ssl_key, ssl_cert, ssl_ca, nullptr, nullptr);

        const char* ssl_version_str = nullptr;
        switch (ssl.version)
        {
        case mxb::ssl_version::TLS11:
            ssl_version_str = "TLSv1.1,TLSv1.2,TLSv1.3";
            break;

        case mxb::ssl_version::TLS12:
            ssl_version_str = "TLSv1.2,TLSv1.3";
            break;

        case mxb::ssl_version::TLS13:
            ssl_version_str = "TLSv1.3";
            break;

        default:
            break;
        }
        if (ssl_version_str)
        {
            mysql_optionsv(newconn, MARIADB_OPT_TLS_VERSION, ssl_version_str);
        }

        if (ssl.verify_peer && ssl.verify_host)
        {
            my_bool verify = 1;
            mysql_optionsv(newconn, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify);
        }
        ssl_enabled = true;
    }

    if (!m_settings.local_address.empty())
    {
        mysql_optionsv(newconn, MYSQL_OPT_BIND, m_settings.local_address.c_str());
    }
    if (m_settings.multiquery)
    {
        mysql_optionsv(newconn, MARIADB_OPT_MULTI_STATEMENTS, (void*) "");
    }
    if (m_settings.auto_reconnect)
    {
        my_bool reconnect = 1;
        mysql_optionsv(newconn, MYSQL_OPT_RECONNECT, &reconnect);
    }
    if (m_settings.clear_sql_mode)
    {
        const char clear_query[] = "SET SQL_MODE='';";
        mysql_optionsv(newconn, MYSQL_INIT_COMMAND, clear_query);
    }
    if (!m_settings.charset.empty())
    {
        mysql_optionsv(newconn, MYSQL_SET_CHARSET_NAME, m_settings.charset.c_str());
    }

    const char* userc   = m_settings.user.c_str();
    const char* passwdc = m_settings.password.c_str();
    const char* dbc     = db.c_str();

    bool connection_success = false;
    if (host.empty() || host[0] != '/')
    {
        const char* hostc = host.empty() ? nullptr : host.c_str();
        // Assume the host is a normal address. Empty host is treated as "localhost".
        if (mysql_real_connect(newconn, hostc, userc, passwdc, dbc, port, nullptr, 0) != nullptr)
        {
            connection_success = true;
        }
    }
    else
    {
        // The host looks like an unix socket.
        if (mysql_real_connect(newconn, nullptr, userc, passwdc, dbc, 0, host.c_str(), 0) != nullptr)
        {
            connection_success = true;
        }
    }

    bool rval = false;
    if (connection_success)
    {
        // If ssl was enabled, check that it's in use.
        bool ssl_ok = !ssl_enabled || (mysql_get_ssl_cipher(newconn) != nullptr);
        if (ssl_ok)
        {
            clear_errors();
            m_conn = newconn;
            rval   = true;
        }
        else
        {
            m_errornum = USER_ERROR;
            m_errormsg = mxb::string_printf("Encrypted connection to [%s]:%i could not be created, "
                                            "ensure that TLS is enabled on the target server.",
                host.c_str(),
                port);
            mysql_close(newconn);
        }
    }
    else
    {
        m_errornum = mysql_errno(newconn);
        m_errormsg = mxb::string_printf("Connection to [%s]:%i failed. Error %li: %s",
            host.c_str(),
            port,
            m_errornum,
            mysql_error(newconn));
        mysql_close(newconn);
    }

    return rval;
}

void MariaDB::close()
{
    if (m_conn)
    {
        mysql_close(m_conn);
        m_conn = nullptr;
    }
}

const char* MariaDB::error() const
{
    return m_errormsg.c_str();
}

int64_t MariaDB::errornum() const
{
    return m_errornum;
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
                m_errormsg         = string_printf(
                    "Query '%s' returned %lu columns and %lu rows of data when none was expected.",
                    sql.c_str(),
                    cols,
                    rows);
                m_errornum = USER_ERROR;
            }
        }
        else
        {
            m_errornum = mysql_errno(m_conn);
            m_errormsg = string_printf(query_failed, sql.c_str(), m_errornum, mysql_error(m_conn));
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
                m_errornum = USER_ERROR;
                m_errormsg = mxb::string_printf(no_data, query.c_str());
            }
        }
        else
        {
            m_errornum = mysql_errno(m_conn);
            m_errormsg = mxb::string_printf(query_failed, query.c_str(), m_errornum, mysql_error(m_conn));
        }
    }
    else
    {
        m_errornum = USER_ERROR;
        m_errormsg = no_connection;
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
            const auto n_queries = queries.size();
            vector<unique_ptr<QueryResult>> results;
            results.reserve(n_queries);

            string errormsg;
            int64_t errornum = 0;

            auto set_error_info = [this, &queries, &errornum, &errormsg](size_t query_ind) {
                auto errored_query
                    = (query_ind < queries.size()) ? queries[query_ind].c_str() : "<unknown-query>";
                auto my_errornum = mysql_errno(m_conn);
                if (my_errornum)
                {
                    errornum = my_errornum;
                    errormsg
                        = string_printf(multiq_elem_failed, errored_query, errornum, mysql_error(m_conn));
                }
                else
                {
                    errornum = USER_ERROR;
                    errormsg = string_printf(multiq_elem_no_data, errored_query);
                }
            };

            bool more_data   = true;
            size_t query_ind = 0;
            // Fetch all resultsets. Check that all individual queries succeed and return valid results.
            while (more_data)
            {
                std::unique_ptr<QueryResult> new_elem;
                MYSQL_RES* result = mysql_store_result(m_conn);
                if (result)
                {
                    new_elem = std::make_unique<mxq::MariaDBQueryResult>(result);
                }
                else if (!errornum)
                {
                    set_error_info(query_ind);
                }
                results.push_back(move(new_elem));
                query_ind++;

                more_data = (mysql_next_result(m_conn) == 0);
                if (!more_data && results.size() < n_queries && !errornum)
                {
                    // Not enough results.
                    set_error_info(query_ind);
                }
            }

            if (!errornum)
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
                    m_errornum = USER_ERROR;
                    m_errormsg = string_printf("Wrong number of resultsets to multiquery '%s'. Got %zi, "
                                               "expected %zi.",
                        multiquery.c_str(),
                        results.size(),
                        n_queries);
                }
            }
            else
            {
                m_errornum = errornum;
                m_errormsg = errormsg;
            }
        }
        else
        {
            m_errornum = mysql_errno(m_conn);
            m_errormsg = string_printf(query_failed, multiquery.c_str(), m_errornum, mysql_error(m_conn));
        }
    }
    else
    {
        m_errornum = USER_ERROR;
        m_errormsg = no_connection;
    }
    return rval;
}

MariaDB::VersionInfo MariaDB::version_info() const
{
    const char* info      = nullptr;
    unsigned long version = 0;
    if (m_conn)
    {
        info    = mysql_get_server_info(m_conn);
        version = mysql_get_server_version(m_conn);
    }
    return VersionInfo {version, info ? info : ""};
}

bool MariaDB::open_extra(const string& host, int port, int extra_port, const string& db)
{
    bool success = open(host, port, db);
    if (!success && m_errornum == ER_CON_COUNT_ERROR && extra_port > 0)
    {
        success = open(host, extra_port, db);
    }
    return success;
}

bool MariaDB::is_open() const
{
    return m_conn != nullptr;
}

bool MariaDB::ping()
{
    bool rval = false;
    if (m_conn)
    {
        if (mysql_ping(m_conn) == 0)
        {
            rval = true;
        }
        else
        {
            m_errornum = mysql_errno(m_conn);
            m_errormsg = mxb::string_printf("Ping failed. Error %li: %s", m_errornum, mysql_error(m_conn));
        }
    }
    else
    {
        m_errornum = USER_ERROR;
        m_errormsg = no_connection;
    }

    return rval;
}

MariaDBQueryResult::MariaDBQueryResult(MYSQL_RES* resultset)
    : QueryResult(column_names(resultset))
    , m_resultset(resultset)
{}

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
    auto columns            = mysql_num_fields(resultset);
    MYSQL_FIELD* field_info = mysql_fetch_fields(resultset);
    for (int64_t column_index = 0; column_index < columns; column_index++)
    {
        rval.emplace_back(field_info[column_index].name);
    }
    return rval;
}
}  // namespace maxsql
