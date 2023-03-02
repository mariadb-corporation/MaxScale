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

namespace
{
GWBUF create_ssl_request()
{
    std::array<uint8_t, 8> buf{};
    pg::set_uint32(buf.data(), 8);
    pg::set_uint32(buf.data() + 4, pg::SSLREQ_MAGIC);
    return GWBUF(buf.data(), buf.size());
}

GWBUF create_startup_message(const uint8_t* params, size_t size)
{
    // The parameters should be null-terminated
    mxb_assert(params[size - 1] == 0x0);

    GWBUF rval(8 + size);
    uint8_t* ptr = rval.data();

    ptr += pg::set_uint32(ptr, 8 + size);
    ptr += pg::set_uint32(ptr, pg::PROTOCOL_V3_MAGIC);
    memcpy(ptr, params, size);

    return rval;
}

GWBUF create_terminate()
{
    std::array<uint8_t, 5> buf{};
    buf[0] = 'X';
    pg::set_uint32(buf.data() + 1, 4);
    return GWBUF(buf.data(), buf.size());
}
}

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
    m_dcb->writeq_append(create_terminate());
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
