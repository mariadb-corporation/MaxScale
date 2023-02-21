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

bool PgProtocolData::is_trx_starting() const
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);

    return false;
}

bool PgProtocolData::is_trx_active() const
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);

    return false;
}

bool PgProtocolData::is_trx_read_only() const
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);

    return false;
}

bool PgProtocolData::is_trx_ending() const
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);

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
