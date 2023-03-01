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

#include "pgbackendconnection.hh"

PgBackendConnection::PgBackendConnection(MXS_SESSION* session, SERVER* server, mxs::Component* component)
    : m_session(session)
    , m_upstream(component)
{
}

void PgBackendConnection::ready_for_reading(DCB* dcb)
{
    if (auto [ok, buf] = m_dcb->read(0, 0); ok)
    {
        mxs::ReplyRoute down;
        m_upstream->clientReply(std::move(buf), down, m_reply);
    }
}

void PgBackendConnection::write_ready(DCB* dcb)
{
    m_dcb->writeq_drain();
}

void PgBackendConnection::error(DCB* dcb)
{
    m_upstream->handleError(mxs::ErrorType::TRANSIENT, "Error", nullptr, m_reply);
}

void PgBackendConnection::hangup(DCB* dcb)
{
    m_upstream->handleError(mxs::ErrorType::TRANSIENT, "Hangup", nullptr, m_reply);
}

bool PgBackendConnection::write(GWBUF&& buffer)
{
    return m_dcb->writeq_append(std::move(buffer));
}

void PgBackendConnection::finish_connection()
{
}

uint64_t PgBackendConnection::can_reuse(MXS_SESSION* session) const
{
    return false;
}

bool PgBackendConnection::reuse(MXS_SESSION* session, mxs::Component* component, uint64_t reuse_type)
{
    m_session = session;
    m_upstream = component;
    return true;
}

bool PgBackendConnection::established()
{
    return true;
}

void PgBackendConnection::set_to_pooled()
{
    m_session = nullptr;
    m_upstream = nullptr;
}

void PgBackendConnection::ping()
{
    // TODO: Figure out what's a good ping mechanism
}

bool PgBackendConnection::can_close() const
{
    return true;
}

void PgBackendConnection::set_dcb(DCB* dcb)
{
    m_dcb = static_cast<BackendDCB*>(dcb);
}

const BackendDCB* PgBackendConnection::dcb() const
{
    return m_dcb;
}

BackendDCB* PgBackendConnection::dcb()
{
    return m_dcb;
}

mxs::Component* PgBackendConnection::upstream() const
{
    return m_upstream;
}

json_t* PgBackendConnection::diagnostics() const
{
    return nullptr;
}

size_t PgBackendConnection::sizeof_buffers() const
{
    return 0;
}
