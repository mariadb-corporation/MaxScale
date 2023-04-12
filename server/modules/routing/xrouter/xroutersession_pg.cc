/*
 * Copyright (c) 2023 MariaDB plc
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
#include "xrouter.hh"
#include "xroutersession.hh"

void XgresSession::preprocess(GWBUF& packet)
{
}

std::string XgresSession::main_sql() const
{
    return "SET xgres.fdw_mode = 'pushdown'";
}

std::string XgresSession::secondary_sql() const
{
    return "SET xgres.fdw_mode = 'import'";
}

std::string XgresSession::lock_sql(std::string_view lock_id) const
{
    return mxb::cat("SELECT pg_advisory_lock(", lock_id, ")");
}

std::string XgresSession::unlock_sql(std::string_view lock_id) const
{
    return mxb::cat("SELECT pg_advisory_unlock(", lock_id, ")");
}
