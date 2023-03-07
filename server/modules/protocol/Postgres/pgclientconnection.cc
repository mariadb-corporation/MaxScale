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

namespace
{
GWBUF create_startup_reply(const GWBUF& startup_message)
{
    // Minimal reply
    const auto auth_len = 1 + 4 + 4;    // Byte1('R'), Int32(8) len, Int32(0) ok
    const auto rdy_len = 1 + 4 + 1;     // Byte1('Z'), Int32(5) len, Byte1('I') for idle
    GWBUF resp{auth_len + rdy_len};

    uint8_t* ptr = resp.data();

    // Authentication OK
    *ptr++ = pg::AUTHENTICATION;
    ptr += pg::set_uint32(ptr, 8);
    ptr += pg::set_uint32(ptr, 0);

    // Ready for query
    *ptr++ = pg::READY_FOR_QUERY;
    ptr += pg::set_uint32(ptr, 5);
    *ptr++ = 'I';   // Idle

    return resp;
}
}

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
        // TODO Deal with this occasional timing issue
        return;
    }

    pg::ExpectCmdByte expect = m_state == State::INIT ? pg::ExpectCmdByte::NO : pg::ExpectCmdByte::YES;

    if (auto [ok, gwbuf] = pg::read_packet(m_dcb, expect); ok && gwbuf)
    {
        switch (m_state)
        {
        case State::INIT:
            m_state = state_init(gwbuf);
            break;

        case State::SSL_HANDSHAKE:
            m_state = state_ssl_handshake(gwbuf);
            break;

        case State::AUTH:
            m_state = state_auth(gwbuf);
            break;

        case State::ROUTE:
            m_state = state_route(std::move(gwbuf));
            break;

        case State::ERROR:
            // pass, handled below
            break;
        }
    }

    // TODO check more states and such, kill session on error
    if (m_state == State::ERROR)
    {
        m_session->kill();
    }
}

PgClientConnection::State PgClientConnection::state_init(const GWBUF& gwbuf)
{
    State next_state = State::ERROR;

    uint32_t first_word = pg::get_uint32(gwbuf.data() + 4);

    if (gwbuf.length() == 8 && first_word == pg::SSLREQ_MAGIC)
    {
        uint8_t authok[] = {pg::SSLREQ_NO};     // TODO no SSL yet
        write(GWBUF {authok, sizeof(authok)});
        next_state = State::INIT;               // Waiting for Startup message
    }
    else
    {
        m_dcb->writeq_append(create_startup_reply(gwbuf));
        // TODO: are there more packets that should be read from the dcb
        //       before going to ROUTE or "normal" state.
        next_state = State::ROUTE;
    }

    return next_state;
}

PgClientConnection::State PgClientConnection::state_ssl_handshake(const GWBUF& gwbuf)
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    return State::ERROR;
}

PgClientConnection::State PgClientConnection::state_auth(const GWBUF& gwbuf)
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    return State::ERROR;
}

PgClientConnection::State PgClientConnection::state_route(GWBUF&& gwbuf)
{
    m_down->routeQuery(std::move(gwbuf));

    return State::ROUTE;
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
    // The client will send the first message
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
