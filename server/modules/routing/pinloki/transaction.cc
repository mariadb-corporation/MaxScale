/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/assert.h>

#include "transaction.hh"

namespace pinloki
{

bool Transaction::add_event(maxsql::RplEvent& rpl_event)
{
    if (!m_in_transaction)
    {
        return false;
    }

    const char* ptr = rpl_event.pBuffer();
    m_trx_buffer.insert(m_trx_buffer.end(), ptr, ptr + rpl_event.buffer_size());

    return true;
}

int64_t Transaction::size() const
{
    return m_trx_buffer.size();
}

void Transaction::begin(const maxsql::Gtid& gtid)
{
    mxb_assert(m_in_transaction == false);

    m_in_transaction = true;
}

WritePosition& Transaction::commit(WritePosition& pos)
{
    mxb_assert(m_in_transaction == true);

    pos.file.seekp(pos.write_pos);
    pos.file.write(m_trx_buffer.data(), m_trx_buffer.size());

    pos.write_pos = pos.file.tellp();
    pos.file.flush();

    m_in_transaction = false;
    m_trx_buffer.clear();

    return pos;
}
}
