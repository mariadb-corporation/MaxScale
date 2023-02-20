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
#include <maxscale/listener.hh>
#include <maxscale/service.hh>
#include "pgparser.hh"
#include "pgprotocoldata.hh"
#include "pgusermanager.hh"

using std::string;
using std::string_view;

namespace
{

void add_packet_auth_request(GWBUF& gwbuf, pg::Auth authentication_method)
{
    const size_t auth_len = 1 + 4 + 4;      // Byte1('R'), Int32(8) len, Int32 auth_method
    std::array<uint8_t, auth_len> data;

    uint8_t* ptr = begin(data);
    *ptr++ = pg::AUTHENTICATION;
    ptr += pg::set_uint32(ptr, 8);
    ptr += pg::set_uint32(ptr, authentication_method);

    gwbuf.append(begin(data), data.size());
}

void add_packet_ready_for_query(GWBUF& gwbuf)
{
    const size_t rdy_len = 1 + 4 + 1;       // Byte1('R'), Int32(8) len, Int8 trx status
    std::array<uint8_t, rdy_len> data;

    uint8_t* ptr = begin(data);
    *ptr++ = pg::READY_FOR_QUERY;
    ptr += pg::set_uint32(ptr, 5);
    *ptr++ = 'I';   // trx idle

    gwbuf.append(begin(data), data.size());
}
}

bool PgClientConnection::validate_cleartext_auth(const GWBUF& reply)
{
    return true;
}

PgClientConnection::PgClientConnection(MXS_SESSION* pSession, mxs::Component* pComponent,
                                       const UserAuthSettings& auth_settings)
    : m_session(*pSession)
    , m_ssl_required(m_session.listener_data()->m_ssl.config().enabled)
    , m_down(pComponent)
    , m_protocol_data(static_cast<PgProtocolData*>(pSession->protocol_data()))
    , m_user_auth_settings(auth_settings)
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

    return m_dcb->ssl_handshake() >= 0;
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

    // TODO: This is not efficient, especially if the client normally sends
    //       multiple packets in State::ROUTE.
    if (!m_dcb->readq_empty())
    {
        m_dcb->trigger_read_event();
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
    else if (parse_startup_message(gwbuf))
    {
        // Add user account check here.
        if (pg_prot_data_auth_method == pg::AUTH_OK)
        {
            next_state = start_session() ? State::ROUTE : State::ERROR;
        }
        else
        {
            GWBUF reply;
            add_packet_auth_request(reply, pg_prot_data_auth_method);
            m_dcb->writeq_append(std::move(reply));
            next_state = State::AUTH;
        }
    }

    return next_state;
}

PgClientConnection::State PgClientConnection::state_auth(const GWBUF& gwbuf)
{
    enum class Result {READY, CONTINUE, ERROR};
    auto result = Result::ERROR;

    switch (pg_prot_data_auth_method)
    {
    case pg::AUTH_CLEARTEXT:
        if (validate_cleartext_auth(gwbuf))
        {
            result = Result::READY;
        }
        break;

    default:
        mxb_assert(!true);
        result = Result::ERROR;
        MXB_SERROR("Unsupported authentication method: " << pg_prot_data_auth_method);
        break;
    }

    auto next_state = State::ERROR;

    switch (result)
    {
    case Result::READY:
        next_state = start_session() ? State::ROUTE : State::ERROR;
        break;

    case Result::ERROR:
        MXB_ERROR("Authentication failed, closing PG client connection.");
        next_state = State::ERROR;
        break;

    case Result::CONTINUE:
        next_state = State::AUTH;
        break;
    }

    return next_state;
}

bool PgClientConnection::start_session()
{
    bool rval = false;
    mxb_assert(m_session.state() == MXS_SESSION::State::CREATED);
    if (m_session.start())
    {
        GWBUF rdy;
        add_packet_auth_request(rdy, pg::AUTH_OK);
        add_packet_ready_for_query(rdy);
        write(std::move(rdy));
        rval = true;
    }
    else
    {
        MXB_ERROR("Could not start session, closing PG client connection.");
    }
    return rval;
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
    if (reply.is_complete())
    {
        if (auto trx_state = reply.get_variable(pg::TRX_STATE_VARIABLE); !trx_state.empty())
        {
            auto data = static_cast<PgProtocolData*>(m_session.protocol_data());

            // If the value is anything other than 'I', a transaction is open.
            data->set_in_trx(trx_state[0] != 'I');
        }
    }

    return write(std::move(buffer));
}

bool PgClientConnection::safe_to_restart() const
{
    // TODO: Add support for restarting
    return false;
}

mxs::Parser* PgClientConnection::parser()
{
    return &PgParser::get();
}

size_t PgClientConnection::sizeof_buffers() const
{
    return 0;
}

bool PgClientConnection::parse_startup_message(const GWBUF& buf)
{
    auto consume_zstring = [](const uint8_t*& ptr, const uint8_t* end){
        std::string_view result;
        if (ptr < end)
        {
            result = pg::get_string(ptr);
            ptr += result.length() + 1;
        }
        return result;
    };

    bool rval = false;
    mxb_assert(buf.length() >= 8);
    string_view username;
    string_view database;
    // StartupMessage: 4 bytes length, 4 bytes magic number, then pairs of strings and finally 0 at end.
    auto ptr = buf.data();
    ptr += 4;   // Length should have already been checked.
    uint32_t protocol_version = pg::get_uint32(ptr);
    ptr += 4;
    const auto end = buf.end();

    if (protocol_version == pg::PROTOCOL_V3_MAGIC && *(end - 1) == '\0')
    {
        bool parse_error = false;
        const auto params_begin = ptr;
        while (ptr < end - 1)
        {
            string_view param_name = consume_zstring(ptr, end);
            string_view param_value = consume_zstring(ptr, end);

            if (!param_name.empty())
            {
                // Only recognize a few parameters. Most of the parameters should be sent as is
                // to backends.
                if (param_name == "user")
                {
                    username = param_value;
                }
                else if (param_name == "database")
                {
                    database = param_value;
                }
            }
            else
            {
                parse_error = true;
                break;
            }
        }

        if (!parse_error && username.length() > 0 && ptr == end - 1)
        {
            m_session.set_user(string(username));
            m_protocol_data->set_default_database(database);
            m_protocol_data->set_connect_params(params_begin, end);
            rval = true;
        }
    }
    return rval;
}

void PgClientConnection::update_user_account_entry()
{
    auto match_host = m_user_auth_settings.match_host_pattern ? PgUserCache::MatchHost::YES :
        PgUserCache::MatchHost::NO;
    auto& ses = m_session;
    auto entry = user_account_cache()->find_user(ses.user(), ses.client_remote(),
                                                 m_protocol_data->default_db(), match_host);

    // Postgres stops authentication if a user entry is not found, so a dummy entry is not required like
    // with MariaDB.
    if (entry.type == UserEntryType::USER_ACCOUNT_OK || entry.type == UserEntryType::NO_AUTH_ID_ENTRY)
    {
        PgAuthenticatorModule* selected_module = find_auth_module(entry.auth_method);
        if (selected_module)
        {
            // Correct plugin is loaded, generate session-specific data.
            MXB_INFO("Client %s matched pg_hba.conf entry at line %i.", m_session.user_and_host().c_str(),
                     entry.line_no);
            m_authenticator = selected_module->create_client_authenticator();
        }
        else
        {
            // Authentication cannot continue in this case.
            entry.type = UserEntryType::METHOD_NOT_SUPPORTED;
            MXB_INFO("Client %s matched pg_hba.conf entry at line %i. Entry uses unsupported authentication "
                     "method '%s'. Cannot authenticate user.",
                     m_session.user_and_host().c_str(), entry.line_no, entry.auth_method.c_str());
        }
    }
    m_protocol_data->set_user_entry(entry);
}

PgAuthenticatorModule* PgClientConnection::find_auth_module(const string& auth_method)
{
    PgAuthenticatorModule* rval = nullptr;
    auto& auth_modules = m_session.listener_data()->m_authenticators;
    for (const auto& auth_module : auth_modules)
    {
        auto protocol_auth = static_cast<PgAuthenticatorModule*>(auth_module.get());
        if (protocol_auth->name() == auth_method)
        {
            // Found correct authenticator for the user entry.
            rval = protocol_auth;
            break;
        }
    }
    return rval;
}

const PgUserCache* PgClientConnection::user_account_cache()
{
    return static_cast<const PgUserCache*>(m_session.service->user_account_cache());
}
