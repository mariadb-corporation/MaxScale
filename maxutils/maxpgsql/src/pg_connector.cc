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
}
