/*
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxpgsql/pg_connector.hh>
#include <utility>
#include <thread>
#include <maxbase/stopwatch.hh>
#include <maxbase/assert.hh>
#include <maxbase/format.hh>
#include <maxbase/string.hh>

using std::string;
using mxb::QueryResult;

namespace
{
const char no_connection[] = "PostgreSQL-connection is not open, cannot perform query.";
const char query_failed[] = "Query '%s' failed. %s";
const char wrong_result_type[] = "Unexpected result type for '%s'. Expected '%s', got '%s'.";

/**
 * QueryResult implementation for the PgSQL-class.
 */
class PgQueryResult : public mxb::QueryResult
{
public:
    explicit PgQueryResult(pg_result* resultset);
    ~PgQueryResult() override;

    int64_t get_col_count() const override;
    int64_t get_row_count() const override;

private:
    const char* row_elem(int64_t column_ind) const override;
    bool        advance_row() override;

    static std::vector<std::string> column_names(pg_result* results);

    pg_result* m_resultset {nullptr};   /**< Underlying result set object */
    int        m_row_ind {-1};          /**< Current row index */
    const int  m_row_count {-1};        /**< Total row count */
};
}

namespace maxpgsql
{
void PgSQL::close()
{
    if (m_conn)
    {
        PQfinish(m_conn);
        m_conn = nullptr;
    }
}

PgSQL::~PgSQL()
{
    close();
}

PgSQL::PgSQL(PgSQL&& conn) noexcept
{
    move_helper(std::move(conn));
}

PgSQL& PgSQL::operator=(PgSQL&& rhs) noexcept
{
    if (this != &rhs)
    {
        move_helper(std::move(rhs));
    }
    return *this;
}

void PgSQL::move_helper(PgSQL&& other)
{
    close();
    m_conn = std::exchange(other.m_conn, nullptr);
}

bool PgSQL::open(const std::string& host, int port, const std::string& db)
{
    mxb_assert(port >= 0);
    close();

    const int n_max_params = 128;
    // Give the settings to libpq as two null-terminated arrays. LibPQ will process the arrays as long as
    // the keywords-element is not null. Null entries or empty strings in the values-array are ignored.
    const char* keywords[n_max_params];
    const char* values[n_max_params];
    int n_params = 0;
    auto add_param = [&n_params, &keywords, &values](const char* param, const char* value) {
        keywords[n_params] = param;
        values[n_params] = value;
        n_params++;
    };

    add_param("host", host.c_str());    // TODO: use hostaddr instead when host is known to be a numeric ip
    string port_str = std::to_string(port);
    add_param("port", port_str.c_str());
    add_param("dbname", db.c_str());
    add_param("application_name", "MaxScale");

    add_param("user", m_settings.user.c_str());
    add_param("password", m_settings.password.c_str());

    string timeout_str = std::to_string(m_settings.connect_timeout);
    add_param("connect_timeout", timeout_str.c_str());

    // If ssl-mode is not defined, the PG connector will try ssl first, then downgrade to unencrypted.
    // TODO: think if this is ok.
    const char mode_str[] = "sslmode";
    const auto& ssl = m_settings.ssl;
    if (ssl.enabled)
    {

        if (ssl.verify_host)
        {
            add_param(mode_str, "verify-full");
        }
        else if (ssl.verify_peer)
        {
            add_param(mode_str, "verify-ca");
        }
        else
        {
            add_param(mode_str, "require");
        }

        add_param("sslcert", ssl.cert.c_str());
        add_param("sslkey", ssl.key.c_str());
        add_param("sslrootcert", ssl.ca.c_str());

        const char* ssl_version_str = nullptr;
        switch (ssl.version)
        {
        case mxb::ssl_version::TLS10:
            ssl_version_str = "TLSv1";
            break;

        case mxb::ssl_version::TLS11:
            ssl_version_str = "TLSv1.1";
            break;

        case mxb::ssl_version::TLS12:
            ssl_version_str = "TLSv1.2";
            break;

        case mxb::ssl_version::TLS13:
            ssl_version_str = "TLSv1.3";
            break;

        case mxb::ssl_version::SSL_TLS_MAX:
        case mxb::ssl_version::SSL_UNKNOWN:
            // Leave empty, causes connection to use at least TLSv1.2. Higher versions may also be used if
            // allowed by backend.
            break;
        }
        if (ssl_version_str)
        {
            add_param("ssl_min_protocol_version", ssl_version_str);
            add_param("ssl_max_protocol_version", ssl_version_str);
        }

        add_param("sslcrl", ssl.crl.c_str());
    }

    add_param(nullptr, nullptr);
    mxb_assert(n_params <= n_max_params - 1);

    m_conn = PQconnectdbParams(keywords, values, 0);
    // Connection object can only be null on OOM, assume it never happens.

    bool rval = false;
    if (PQstatus(m_conn) == CONNECTION_OK)
    {
        rval = true;
        m_errormsg.clear();
    }
    else
    {
        m_errormsg = read_pg_error();
    }
    return rval;
}

const char* PgSQL::error() const
{
    return m_errormsg.c_str();
}

bool PgSQL::ping()
{
    bool rval = false;
    // PostgreSQL does not seem to have a similar ping-function as MariaDB. Try a simple query instead.
    auto result = query("select 1;");
    if (result)
    {
        rval = true;
    }
    return rval;
}

bool PgSQL::is_open() const
{
    return m_conn && PQstatus(m_conn) == CONNECTION_OK;
}

PgSQL::ConnectionSettings& PgSQL::connection_settings()
{
    return m_settings;
}

PgSQL::VersionInfo PgSQL::get_version_info()
{
    VersionInfo rval;
    if (is_open())
    {
        rval.version = PQserverVersion(m_conn);
        auto info_res = query("select version();");
        if (info_res)
        {
            if (info_res->next_row() && info_res->get_col_count() == 1)
            {
                rval.info = info_res->get_string(0);
                m_errormsg.clear();
            }
            else
            {
                m_errormsg = "Invalid version result.";
            }
        }
    }
    else
    {
        m_errormsg = no_connection;
    }
    return rval;
}

bool PgSQL::cmd(const string& query)
{
    bool rval = false;
    if (m_conn)
    {
        auto result = PQexec_with_timeout(query);
        auto res_status = PQresultStatus(result);   // ok even if result is null
        if (res_status == PGRES_COMMAND_OK)
        {
            rval = true;
            m_errormsg.clear();
        }
        else if (res_status == PGRES_TUPLES_OK)
        {
            int cols = PQnfields(result);
            int rows = PQntuples(result);
            m_errormsg = mxb::string_printf(
                "Command '%s' returned %d columns and %d rows of data when none was expected.",
                query.c_str(), cols, rows);
        }
        else if (res_status == PGRES_FATAL_ERROR)
        {
            // If the error message is not empty, the PQexec_with_timeout call generated a custom error.
            // If it's empty, the result may not exist, ask connection itself for error.
            std::string msg = m_errormsg.empty() ? read_pg_error() : m_errormsg;
            m_errormsg = mxb::string_printf(query_failed, query.c_str(), msg.c_str());
        }
        else
        {
            // Ask the result object for error.
            const char* errmsg = PQresultErrorMessage(result);
            if (*errmsg)
            {
                m_errormsg = mxb::string_printf(query_failed, query.c_str(), errmsg);
            }
            else
            {
                // Not an error, must be some other unexpected result type.
                const char* expected = PQresStatus(PGRES_COMMAND_OK);
                const char* found = PQresStatus(res_status);
                m_errormsg = mxb::string_printf(wrong_result_type, query.c_str(), expected, found);
            }
        }
        PQclear(result);
        // TODO: log statements
    }
    else
    {
        m_errormsg = no_connection;
    }

    return rval;
}

std::unique_ptr<QueryResult> PgSQL::query(const std::string& query)
{
    std::unique_ptr<QueryResult> rval;
    if (m_conn)
    {
        auto result = PQexec_with_timeout(query);
        auto res_status = PQresultStatus(result);
        if (res_status == PGRES_TUPLES_OK)
        {
            rval = std::make_unique<PgQueryResult>(result);
            m_errormsg.clear();
        }
        else if (res_status == PGRES_FATAL_ERROR)
        {
            // Gets here also if result is null.
            if (m_errormsg.empty())
            {
                if (result)
                {
                    const char* errmsg = PQresultErrorMessage(result);
                    if (*errmsg)
                    {
                        m_errormsg = mxb::string_printf(query_failed, query.c_str(), errmsg);
                    }
                    PQclear(result);
                }

                if (m_errormsg.empty())
                {
                    // Still no error message. Try to get one from the connection object.
                    m_errormsg = mxb::string_printf(query_failed, query.c_str(), read_pg_error().c_str());
                }
            }
            else
            {
                mxb_assert(!result);
                // PQexec_with_timeout() generated a custom error. This only happens with serious errors
                // which require a reconnection as the result may not have been read out.
                m_errormsg = mxb::string_printf(query_failed, query.c_str(), m_errormsg.c_str());
                PQreset(m_conn);
            }
        }
        else
        {
            const char* errmsg = PQresultErrorMessage(result);
            if (*errmsg)
            {
                m_errormsg = mxb::string_printf(query_failed, query.c_str(), errmsg);
            }
            else
            {
                const char* expected = PQresStatus(PGRES_TUPLES_OK);
                const char* found = PQresStatus(res_status);
                m_errormsg = mxb::string_printf(wrong_result_type, query.c_str(), expected, found);
            }
            PQclear(result);
        }
        // TODO: log statements
    }
    else
    {
        m_errormsg = no_connection;
    }
    return rval;
}

std::string PgSQL::read_pg_error() const
{
    // PG error messages can contain linebreaks. Replace the linebreaks with space. If the linebreak is at
    // the end of the error message, erase it.
    mxb_assert(m_conn);
    string errormsg = PQerrorMessage(m_conn);
    std::replace(errormsg.begin(), errormsg.end(), '\n', ' ');
    mxb::rtrim(errormsg);
    return errormsg;
}

PGresult* PgSQL::PQexec_with_timeout(const std::string& query)
{
    // Clear out the error. This way the calling code can inspect the error message to determine whether
    // this function created a custom, non-libpq error message.
    m_errormsg.clear();

    std::chrono::seconds write_limit(m_settings.write_timeout);
    std::chrono::seconds read_limit(m_settings.read_timeout);
    auto start = mxb::SteadyClock::now();

    PQsetnonblocking(m_conn, 1);
    PQsendQuery(m_conn, query.c_str());

    // TODO: Using select() or poll() for this would be nicer.

    while (PQflush(m_conn) == 1)
    {
        if (mxb::SteadyClock::now() - start > write_limit)
        {
            m_errormsg = "Sending query to the server timed out.";
            PQsetnonblocking(m_conn, 0);
            return nullptr;
        }

        std::this_thread::sleep_for(10ms);
    }

    start = mxb::SteadyClock::now();

    do
    {
        if (PQconsumeInput(m_conn) == 0)
        {
            return nullptr;
        }

        if (PQisBusy(m_conn) == 1)
        {
            if (mxb::SteadyClock::now() - start > read_limit)
            {
                m_errormsg = "Reading result from the server timed out.";
                PQsetnonblocking(m_conn, 0);
                return nullptr;
            }

            std::this_thread::sleep_for(10ms);
        }
        else
        {
            break;
        }
    }
    while (true);

    auto result = PQgetResult(m_conn);

    // Discard trailing results to behave like PQexec
    while (auto res = PQgetResult(m_conn))
    {
        PQclear(res);
    }

    PQsetnonblocking(m_conn, 0);
    return result;
}
}

namespace
{
PgQueryResult::PgQueryResult(pg_result* resultset)
    : QueryResult(column_names(resultset))
    , m_resultset(resultset)
    , m_row_count(PQntuples(resultset))
{
}

std::vector<std::string> PgQueryResult::column_names(pg_result* results)
{
    std::vector<std::string> rval;
    int n_columns = PQnfields(results);
    rval.resize(n_columns);
    for (int i = 0; i < n_columns; i++)
    {
        rval[i] = PQfname(results, i);
    }
    return rval;
}

PgQueryResult::~PgQueryResult()
{
    PQclear(m_resultset);
}

int64_t PgQueryResult::get_col_count() const
{
    return PQnfields(m_resultset);
}

int64_t PgQueryResult::get_row_count() const
{
    return m_row_count;
}

bool PgQueryResult::advance_row()
{
    bool rval = false;
    if (m_row_ind < m_row_count - 1)
    {
        m_row_ind++;
        rval = true;
    }
    return rval;
}

const char* PgQueryResult::row_elem(int64_t column_ind) const
{
    // TODO: This only works for text-format data.
    return PQgetvalue(m_resultset, m_row_ind, column_ind);
}
}
