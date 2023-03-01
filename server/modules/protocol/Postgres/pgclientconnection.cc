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

PgClientConnection::PgClientConnection(MXS_SESSION* pSession, mxs::Component* pComponent)
    : m_session(pSession)
    , m_down(pComponent)
{
}

void PgClientConnection::ready_for_reading(DCB* dcb)
{
    mxb_assert(m_dcb == dcb);

    if (m_session->state() == MXS_SESSION::State::CREATED && !m_session->start())
    {
        m_session->kill();
    }
    else if (auto [ok, buf] = m_dcb->read(0, 0); ok && buf)
    {
        m_down->routeQuery(std::move(buf));
    }
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
    // TODO: Add some logging in case we didn't expect this
    m_session->kill();
}

void PgClientConnection::hangup(DCB* dcb)
{
    // TODO: Add some logging in case we didn't expect this
    m_session->kill();
}

bool PgClientConnection::write(GWBUF&& buffer)
{
    return m_dcb->writeq_append(std::move(buffer));
}

bool PgClientConnection::init_connection()
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    return true;
}

void PgClientConnection::finish_connection()
{
    // TODO: Do something?
}

bool PgClientConnection::clientReply(GWBUF&& buffer,
                                     mxs::ReplyRoute& down,
                                     const mxs::Reply& reply)
{
    return write(std::move(buffer));
}

bool PgClientConnection::safe_to_restart() const
{
    // TODO: Add support for restarting
    return false;
}

size_t PgClientConnection::sizeof_buffers() const
{
    return 0;
}
