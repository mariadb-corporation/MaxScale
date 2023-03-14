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
#include "pgprotocoldata.hh"
#include <maxscale/dcb.hh>
#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include <maxscale/listener.hh>
#include <maxscale/service.hh>

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
    : m_session(*pSession)
    , m_ssl_required(m_session.listener_data()->m_ssl.config().enabled)
    , m_down(pComponent)
{
}

bool PgClientConnection::setup_ssl()
{
    auto state = m_dcb->ssl_state();
    mxb_assert(state != DCB::SSLState::ESTABLISHED);

    if (state == DCB::SSLState::HANDSHAKE_UNKNOWN)
    {
        m_dcb->set_ssl_state(DCB::SSLState::HANDSHAKE_REQUIRED);
    }

    auto rv = m_dcb->ssl_handshake();

    const char* zRemote = m_dcb->remote().c_str();
    const char* zService = m_session.service->name();

    return rv >= 0;
}

void PgClientConnection::ready_for_reading(DCB* dcb)
{
    mxb_assert(m_dcb == dcb);

    pg::ExpectCmdByte expect = m_state == State::INIT ? pg::ExpectCmdByte::NO : pg::ExpectCmdByte::YES;

    if (auto [ok, gwbuf] = pg::read_packet(m_dcb, expect); ok && gwbuf)
    {
        switch (m_state)
        {
        case State::INIT:
            m_state = state_init(gwbuf);
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

    if (m_state == State::ERROR)
    {
        m_session.kill();
    }
}

PgClientConnection::State PgClientConnection::state_init(const GWBUF& gwbuf)
{
    State next_state = State::ERROR;

    uint32_t first_word = pg::get_uint32(gwbuf.data() + 4);

    if (gwbuf.length() == 8 && first_word == pg::SSLREQ_MAGIC)
    {
        uint8_t auth_resp[] = {m_ssl_required ? pg::SSLREQ_YES : pg::SSLREQ_NO};
        write(GWBUF {auth_resp, sizeof(auth_resp)});

        if (m_ssl_required && !setup_ssl())
        {
            MXB_ERROR("SSL setup failed, closing PG client connection.");
            next_state = State::ERROR;
        }
        else
        {
            next_state = State::INIT;   // Waiting for Startup message
        }
    }
    else
    {
        auto data = static_cast<PgProtocolData*>(m_session.protocol_data());
        data->set_connect_params(gwbuf);

        m_dcb->writeq_append(create_startup_reply(gwbuf));
        // TODO: are there more packets that should be read from the dcb
        //       before going to ROUTE or "normal" state.
        if (m_session.state() == MXS_SESSION::State::CREATED && m_session.start())
        {
            next_state = State::ROUTE;
        }
        else
        {
            MXB_ERROR("Could not start session, closing PG client connection.");
            next_state = State::ERROR;
        }
    }

    return next_state;
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
    m_session.kill();
}

void PgClientConnection::hangup(DCB* dcb)
{
    // TODO: Add some logging in case we didn't expect this
    m_session.kill();
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

mxs::Parser* PgClientConnection::parser()
{
    return &MariaDBParser::get();
}

size_t PgClientConnection::sizeof_buffers() const
{
    return 0;
}
