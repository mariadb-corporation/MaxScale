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
    bool keep_going = true;

    while (keep_going)
    {
        switch (m_state)
        {
        case State::SSL_REQUEST:
            keep_going = handle_ssl_request();
            break;

        case State::SSL_HANDSHAKE:
            keep_going = handle_ssl_handshake();
            break;

        case State::STARTUP:
            keep_going = handle_startup();
            break;

        case State::AUTH:
            keep_going = handle_auth();
            break;

        case State::BACKLOG:
            keep_going = handle_backlog();
            break;

        case State::ROUTING:
            keep_going = handle_routing();
            break;

        case State::FAILED:
            keep_going = false;
            break;

        case State::INIT:
            mxb_assert_message(!true, "We should not end up here");
            handle_error("Internal error");
            keep_going = false;
            break;
        }
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

    if (m_state == State::INIT)
    {
        // In the Postgres protocol, the client starts by sending a message
        if (m_dcb->using_ssl())
        {
            // If the server is configured to use TLS, send a SSLRequest message to see if the server has been
            // configured with TLS.
            send_ssl_request();
        }
        else
        {
            // If TLS is not configured, skip it and send the StartupMessage immediately
            send_startup_message();
        }
    }
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

void PgBackendConnection::handle_error(const std::string& error, mxs::ErrorType type)
{
    m_upstream->handleError(type, error, nullptr, m_reply);
    m_state = State::FAILED;
}

void PgBackendConnection::send_ssl_request()
{
    if (m_dcb->writeq_append(create_ssl_request()))
    {
        m_state = State::SSL_REQUEST;
    }
    else
    {
        handle_error("Failed to write SSL request");
    }
}

void PgBackendConnection::send_startup_message()
{
    // TODO: Copy these from the client. This'll only work if there's role named "maxuser".
    const char params[] = "user\0maxuser\0";

    // The parameters are a list of null-terminated strings that end with an empty string
    if (m_dcb->writeq_append(create_startup_message((uint8_t*)params, sizeof(params))))
    {
        m_state = State::STARTUP;
    }
    else
    {
        handle_error("Failed to write startup message");
    }
}

bool PgBackendConnection::handle_ssl_request()
{
    if (auto [ok, buf] = m_dcb->read_strict(1, 1); ok)
    {
        mxb_assert_message(buf, "There should always be data available");
        uint8_t response = buf[0];

        if (response == pg::SSLREQ_NO)
        {
            // No SSL, send the normal startup message.
            send_startup_message();
        }
        else if (response == pg::SSLREQ_YES)
        {
            // SSL requested, start the TLS handshake.
            if (m_dcb->ssl_handshake() == -1)
            {
                handle_error("TLS handshake failed");
            }
            else
            {
                m_state = State::SSL_HANDSHAKE;
            }
        }
        else
        {
            handle_error("Unknown response to SSL request");
        }
    }
    else
    {
        handle_error("Network read failed");
    }

    return m_state != State::FAILED;
}

bool PgBackendConnection::handle_ssl_handshake()
{
    bool keep_going = false;

    switch (m_dcb->ssl_state())
    {
    case DCB::SSLState::ESTABLISHED:
        send_startup_message();
        keep_going = true;
        break;

    case DCB::SSLState::HANDSHAKE_REQUIRED:
        // Handshake is still going on, wait for more data.
        break;

    default:
        handle_error("SSL handshake failed");
        break;
    }

    return keep_going;
}

bool PgBackendConnection::handle_startup()
{
    handle_error("Not yet implemented");
    return false;
}

bool PgBackendConnection::handle_auth()
{
    handle_error("Not yet implemented");
    return false;
}

bool PgBackendConnection::handle_backlog()
{
    handle_error("Not yet implemented");
    return false;
}

bool PgBackendConnection::handle_routing()
{
    handle_error("Not yet implemented");
    return false;
}
