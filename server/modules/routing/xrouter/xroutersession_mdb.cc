/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "xrouter.hh"
#include "xroutersession.hh"

void XmSession::preprocess(GWBUF&)
{
}

std::string XmSession::main_sql() const
{
    return "SET @fdw_mode = 'pushdown'";
}

std::string XmSession::secondary_sql() const
{
    return "SET @fdw_mode = 'import'";
}

std::string XmSession::lock_sql(std::string_view lock_id) const
{
    // Ten years ought to be enough of a timeout
    return mxb::cat("SELECT GET_LOCK('", lock_id, "', 315360000)");
}

std::string XmSession::unlock_sql(std::string_view lock_id) const
{
    return mxb::cat("SELECT RELEASE_LOCK('", lock_id, "')");
}
