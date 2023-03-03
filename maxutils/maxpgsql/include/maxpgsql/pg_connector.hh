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
#pragma once

#include <maxpgsql/ccdefs.hh>

#include <memory>
#include <string>

struct pg_conn;

namespace maxpgsql
{
/**
 * Convenience class for working with Pg connections.
 */
class PgSQL final
{
public:
    PgSQL() = default;
    virtual ~PgSQL();
    PgSQL(const PgSQL& rhs) = delete;
    PgSQL& operator=(const PgSQL& rhs) = delete;

    PgSQL(PgSQL&& conn) noexcept;
    PgSQL& operator=(PgSQL&& rhs) noexcept;

    void close();

private:
    pg_conn* m_conn;

    void move_helper(PgSQL&& other);
};
}
