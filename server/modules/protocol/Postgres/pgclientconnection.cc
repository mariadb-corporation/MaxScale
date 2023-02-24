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

#include "pgclientconnection.hh"
#include <maxscale/dcb.hh>

void PgClientConnection::ready_for_reading(DCB* dcb)
{
    mxb_assert(m_dcb == dcb);

    // TODO: Depending on state (authentication not yet done, in process, or done, etc.)
    // TODO: do whatever should be done.

    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);
}

void PgClientConnection::write_ready(DCB* dcb)
{
    mxb_assert(m_dcb == dcb);
    mxb_assert(m_dcb->state() != DCB::State::DISCONNECTED);

    // TODO: Probably some state handling is needed.

    m_dcb->writeq_drain();
}

void PgClientConnection::error(DCB* dcb)
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);
}

void PgClientConnection::hangup(DCB* dcb)
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);
}

int32_t PgClientConnection::write(GWBUF* buffer)
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);
    return 0;
}

bool PgClientConnection::write(GWBUF&& buffer)
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);
    return 0;
}

bool PgClientConnection::init_connection()
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    return true;
}

void PgClientConnection::finish_connection()
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);
}

bool PgClientConnection::clientReply(GWBUF&& buffer,
                                     mxs::ReplyRoute& down,
                                     const mxs::Reply& reply)
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);
    return false;
}

bool PgClientConnection::safe_to_restart() const
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);
    return true;
}

size_t PgClientConnection::sizeof_buffers() const
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);
    return 0;
}
