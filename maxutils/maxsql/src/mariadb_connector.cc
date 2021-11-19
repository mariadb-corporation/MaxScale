/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
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
#include <errmsg.h>

using std::move;
using std::string;
using std::vector;
using std::unique_ptr;
using mxb::string_printf;
using mxq::QueryResult;

namespace
{
const char no_connection[] = "MySQL-connection is not open, cannot perform query.";
const char query_failed[] = "Query '%s' failed. Error %li: %s.";
const char multiq_elem_failed[] = "Multiquery element '%s' failed. Error %li: %s.";
const char no_data[] = "Query '%s' did not return any results.";
const char multiq_elem_no_data[] = "Multiquery element '%s' did not return any results.";

/**
 * Default plugin directory. Used by the MariaDB-class if a plugin directory is not given in connection
 * settings. If the default is left empty, Connector-C will search its build directory. This is fine for
 * system tests as they are ran from the build directory.
 *
 * If plugins are required in an installed program, then it should set the default directory before using
 * the class.
 */
std::string default_plugin_dir;
}

namespace maxsql
{

// static
void MariaDB::set_default_plugin_dir(std::string&& dir)
{
    default_plugin_dir = move(dir);
}

MariaDB::~MariaDB()
{
    close();
}

bool MariaDB::open(const std::string& host, int port, const std::string& db)
{
    mxb_assert(port >= 0);      // MaxScale config loader should not accept negative values. 0 is ok.
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

    const string& eff_plugin_dir = m_settings.plugin_dir.empty() ? default_plugin_dir :
        m_settings.plugin_dir;
    if (!eff_plugin_dir.empty())
    {
        mysql_optionsv(newconn, MYSQL_PLUGIN_DIR, eff_plugin_dir.c_str());
    }

    bool ssl_enabled = m_settings.ssl.enabled;
    if (ssl_enabled)
    {
        // If an option is empty, a null-pointer should be given to mysql_ssl_set.
        auto& ssl = m_settings.ssl;
        const char* ssl_key = ssl.key.empty() ? nullptr : ssl.key.c_str();
        const char* ssl_cert = ssl.cert.empty() ? nullptr : ssl.cert.c_str();
        const char* ssl_ca = ssl.ca.empty() ? nullptr : ssl.ca.c_str();
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

        if (!ssl.crl.empty())
        {
            mysql_optionsv(newconn, MYSQL_OPT_SSL_CRL, ssl.crl.c_str());
        }

        if (!ssl.cipher.empty())
        {
            mysql_optionsv(newconn, MYSQL_OPT_SSL_CIPHER, ssl.cipher.c_str());
        }
    }

    if (!m_settings.local_address.empty())
    {
        mysql_optionsv(newconn, MYSQL_OPT_BIND, m_settings.local_address.c_str());
    }
    if (m_settings.multiquery)
    {
        mysql_optionsv(newconn, MARIADB_OPT_MULTI_STATEMENTS, (void*)"");
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

    const char* userc = m_settings.user.c_str();
    const char* passwdc = m_settings.password.c_str();
    const char* dbc = db.c_str();

    bool connection_success = false;
    int opts = CLIENT_REMEMBER_OPTIONS;

    if (host.empty() || host[0] != '/')
    {
        const char* hostc = host.empty() ? nullptr : host.c_str();
        // Assume the host is a normal address. Empty host is treated as "localhost".
        if (mysql_real_connect(newconn, hostc, userc, passwdc, dbc, port, nullptr, opts) != nullptr)
        {
            connection_success = true;
        }
    }
    else
    {
        // The host looks like an unix socket.
        if (mysql_real_connect(newconn, nullptr, userc, passwdc, dbc, 0, host.c_str(), opts) != nullptr)
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
            rval = true;
        }
        else
        {
            m_errornum = USER_ERROR;
            m_errormsg = mxb::string_printf("Encrypted connection to [%s]:%i could not be created, "
                                            "ensure that TLS is enabled on the target server.",
                                            host.c_str(), port);
            mysql_close(newconn);
        }
    }
    else
    {
        m_errornum = mysql_errno(newconn);
        m_errormsg = mxb::string_printf("Connection to [%s]:%i failed. Error %li: %s",
                                        host.c_str(), port, m_errornum, mysql_error(newconn));
        mysql_close(newconn);
    }

    return rval;
}

void MariaDB::close()
{
    mysql_free_result(m_current_result);
    m_current_result = nullptr;

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

bool MariaDB::cmd(const std::string& query)
{
    auto result_handler = [this, &query]() {
            bool rval = false;
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
                m_errornum = USER_ERROR;
                m_errormsg = string_printf(
                    "Query '%s' returned %lu columns and %lu rows of data when none was expected.",
                    query.c_str(), cols, rows);
                mysql_free_result(result);
            }
            return rval;
        };

    return run_query(query, result_handler);
}

std::unique_ptr<mxq::QueryResult> MariaDB::query(const std::string& query)
{
    std::unique_ptr<QueryResult> rval;
    auto result_handler = [this, &query, &rval]() {
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
            return true;
        };

    run_query(query, result_handler);
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
        int rc = mysql_real_query(m_conn, multiquery.c_str(), multiquery.length());

        if (rc == 0)
        {
            const auto n_queries = queries.size();
            vector<unique_ptr<QueryResult>> results;
            results.reserve(n_queries);

            string errormsg;
            int64_t errornum = 0;

            auto set_error_info = [this, &queries, &errornum, &errormsg](size_t query_ind) {
                    auto errored_query = (query_ind < queries.size()) ? queries[query_ind].c_str() :
                        "<unknown-query>";
                    auto my_errornum = mysql_errno(m_conn);
                    if (my_errornum)
                    {
                        errornum = my_errornum;
                        errormsg = string_printf(multiq_elem_failed,
                                                 errored_query, errornum, mysql_error(m_conn));
                    }
                    else
                    {
                        errornum = USER_ERROR;
                        errormsg = string_printf(multiq_elem_no_data, errored_query);
                    }
                };

            bool more_data = true;
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
                                               multiquery.c_str(), results.size(), n_queries);
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

        mxq::log_statement(rc, m_conn, multiquery);
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
    const char* info = nullptr;
    unsigned long version = 0;
    if (m_conn)
    {
        info = mysql_get_server_info(m_conn);
        version = mysql_get_server_version(m_conn);
    }
    return VersionInfo {version, info ? info : ""};
}

bool MariaDB::open_extra(const string& host, int port, int extra_port, const string& db)
{
    bool success = false;
    if (extra_port > 0)
    {
        success = open(host, extra_port, db);
        if (!success && (m_errornum == ER_CON_COUNT_ERROR || m_errornum == CR_CONNECTION_ERROR))
        {
            success = open(host, port, db);
        }
    }
    else
    {
        success = open(host, port, db);
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

bool MariaDB::change_user(const string& user, const string& pw, const string& db)
{
    bool rval = false;
    if (is_open())
    {
        rval = (mysql_change_user(m_conn, user.c_str(), pw.c_str(), db.c_str()) == 0);
    }
    return rval;
}

bool MariaDB::reconnect()
{
    bool rval = false;
    if (m_conn)
    {
        const char yes = 1;
        mysql_optionsv(m_conn, MYSQL_OPT_RECONNECT, &yes);

        if (mariadb_reconnect(m_conn) == 0)
        {
            rval = true;
        }
        else
        {
            m_errornum = mysql_errno(m_conn);
            m_errormsg = mxb::string_printf("Reconnect failed. Error %li: %s",
                                            m_errornum, mysql_error(m_conn));
        }

        const char no = 0;
        mysql_optionsv(m_conn, MYSQL_OPT_RECONNECT, &no);
    }
    else
    {
        m_errornum = USER_ERROR;
        m_errormsg = no_connection;
    }

    return rval;
}

bool MariaDB::run_query(const string& query, const std::function<bool()>& result_handler)
{
    bool rval = false;
    if (m_conn)
    {
        int rc = mysql_real_query(m_conn, query.c_str(), query.length());

        if (rc == 0)
        {
            rval = result_handler();
        }
        else
        {
            m_errornum = mysql_errno(m_conn);
            m_errormsg = mxb::string_printf(query_failed, query.c_str(), m_errornum, mysql_error(m_conn));
        }

        mxq::log_statement(rc, m_conn, query);
    }
    else
    {
        m_errornum = USER_ERROR;
        m_errormsg = no_connection;
    }

    return rval;
}

MariaDB::ResultType MariaDB::streamed_query(const string& query)
{
    auto result_handler = [this]() {
            update_multiq_result_type();
            return true;
        };

    if (!run_query(query, result_handler))
    {
        // The result handler is not ran if the query fails. In this case, preserve the normal error
        // message and set the result type manually.
        m_current_result_type = ResultType::ERROR;
    }
    // Return the type of the result immediately. If the query is a multiquery, this is the result type of
    // the first multiquery element.
    return m_current_result_type;
}

MariaDB& MariaDB::operator=(MariaDB&& rhs) noexcept
{
    mxb_assert(this != &rhs);
    close();
    m_conn = rhs.m_conn;
    rhs.m_conn = nullptr;

    m_current_result = rhs.m_current_result;
    rhs.m_current_result = nullptr;
    m_current_result_type = rhs.m_current_result_type;

    m_settings = move(rhs.m_settings);
    m_errornum = rhs.m_errornum;
    m_errormsg = move(rhs.m_errormsg);
    return *this;
}

void MariaDB::update_multiq_result_type()
{
    ResultType new_type;
    m_errornum = mysql_errno(m_conn);   // Can change when advancing to next result.
    if (m_errornum != 0)
    {
        // Don't know the exact query which failed.
        m_errormsg = mxb::string_printf("Multiquery element failed. Error %li: %s.",
                                        m_errornum, mysql_error(m_conn));
        new_type = ResultType::ERROR;
    }
    else
    {
        auto results = mysql_use_result(m_conn);
        if (results)
        {
            new_type = ResultType::RESULTSET;
            m_current_result = results;
        }
        else
        {
            new_type = ResultType::OK;
        }
    }
    m_current_result_type = new_type;
}

MariaDB::ResultType MariaDB::next_result()
{
    if (m_current_result)
    {
        // Current resultset has not been read out, free it.
        mysql_free_result(m_current_result);
        m_current_result = nullptr;
    }

    if (mysql_more_results(m_conn))
    {
        mysql_next_result(m_conn);
        update_multiq_result_type();
    }
    else
    {
        m_current_result_type = ResultType::NONE;
    }
    return m_current_result_type;
}

std::unique_ptr<mxq::MariaDBQueryResult> MariaDB::get_resultset()
{
    auto rval = std::make_unique<mxq::MariaDBQueryResult>(m_current_result);
    m_current_result = nullptr;
    return rval;
}

std::unique_ptr<mxq::MariaDBOkResult> MariaDB::get_ok_result()
{
    auto rval = std::make_unique<mxq::MariaDBOkResult>();
    rval->insert_id = mysql_insert_id(m_conn);
    rval->warnings = mysql_warning_count(m_conn);
    rval->affected_rows = mysql_affected_rows(m_conn);
    return rval;
}

std::unique_ptr<mxq::MariaDBErrorResult> MariaDB::get_error_result()
{
    auto rval = std::make_unique<MariaDBErrorResult>();
    rval->error_num = mysql_errno(m_conn);
    rval->error_msg = mysql_error(m_conn);
    rval->sqlstate = mysql_sqlstate(m_conn);
    return rval;
}

MariaDB::ResultType MariaDB::current_result_type()
{
    return m_current_result_type;
}

MariaDB::MariaDB(MariaDB&& conn) noexcept
{
    *this = move(conn);
}

MariaDBQueryResult::MariaDBQueryResult(MYSQL_RES* resultset)
    : QueryResult(column_names(resultset))
    , m_resultset(resultset)
{
    prepare_fields_info();
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

const MariaDBQueryResult::Fields& MariaDBQueryResult::fields() const
{
    return m_fields_info;
}

const char* const* MariaDBQueryResult::rowdata() const
{
    return m_rowdata;
}

void MariaDBQueryResult::prepare_fields_info()
{
    using Type = Field::Type;
    auto n = mysql_num_fields(m_resultset);
    auto fields = mysql_fetch_fields(m_resultset);
    m_fields_info.reserve(n);

    for (unsigned int i = 0; i < n; i++)
    {
        auto resolved_type = Type::OTHER;
        auto field = fields[i];

        // Not set in stone, add more if needed.
        switch (field.type)
        {
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_INT24:
            resolved_type = Type::INTEGER;
            break;

        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
            resolved_type = Type::FLOAT;
            break;

        case MYSQL_TYPE_NULL:
            resolved_type = Type::NUL;
            break;

        default:
            break;
        }

        Field new_elem = {field.name, resolved_type};
        m_fields_info.push_back(std::move(new_elem));
    }
}
}
