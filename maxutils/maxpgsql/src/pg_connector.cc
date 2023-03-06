/*
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxpgsql/pg_connector.hh>
#include <libpq-fe.h>
#include <utility>
#include <maxbase/assert.hh>
#include <maxbase/format.hh>

using std::string;

namespace
{
const char no_connection[] = "PostgreSQL-connection does not exist.";
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

    string port_str = std::to_string(port);
    m_conn = PQsetdbLogin(host.c_str(), port_str.c_str(), "", "", "",
                          m_settings.user.c_str(), m_settings.password.c_str());
    // Connection object can only be null on OOM, assume it never happens.
    // TODO: add other options e.g. ssl

    bool rval = false;
    if (PQstatus(m_conn) == CONNECTION_OK)
    {
        rval = true;
    }
    return rval;
}

const char* PgSQL::error() const
{
    const char* rval = no_connection;
    if (m_conn)
    {
        rval = PQerrorMessage(m_conn);
    }
    return rval;
}

bool PgSQL::ping()
{
    bool rval = false;
    // PostgreSQL does not seem to have a similar ping-function as MariaDB. Try a simple query instead.
    if (m_conn)
    {
        auto result = PQexec(m_conn, "select 1;");
        auto res_status = PQresultStatus(result);
        if (res_status == PGRES_TUPLES_OK)
        {
            rval = true;
        }
        PQclear(result);
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
}
