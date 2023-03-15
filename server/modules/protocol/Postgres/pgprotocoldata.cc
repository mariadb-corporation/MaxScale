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

#include "pgprotocoldata.hh"

PgProtocolData::~PgProtocolData()
{
}

bool PgProtocolData::will_respond(const GWBUF& buffer) const
{
    return pg::will_respond(buffer);
}

bool PgProtocolData::can_recover_state() const
{
    // TODO: Change this to a call into mxs::History::can_recover_state()
    return false;
}

bool PgProtocolData::is_trx_starting() const
{
    return false;
}

bool PgProtocolData::is_trx_active() const
{
    return false;
}

bool PgProtocolData::is_trx_read_only() const
{
    return false;
}

bool PgProtocolData::is_trx_ending() const
{
    return false;
}

bool PgProtocolData::is_autocommit() const
{
    return false;
}

size_t PgProtocolData::amend_memory_statistics(json_t* memory) const
{
    return runtime_size();
}

size_t PgProtocolData::static_size() const
{
    return sizeof(*this);
}

size_t PgProtocolData::varying_size() const
{
    return 0;
}
