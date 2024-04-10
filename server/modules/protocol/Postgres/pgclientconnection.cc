/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pgclientconnection.hh"
#include "pgbackendconnection.hh"
#include <maxbase/format.hh>
#include <maxscale/dcb.hh>
#include <maxscale/listener.hh>
#include <maxscale/service.hh>
#include <maxscale/utils.hh>
#include <unistd.h>
#include "pgprotocoldata.hh"
#include "pgusermanager.hh"

using std::string;
using std::string_view;

namespace
{
const string invalid_auth = "28000";    // invalid_authorization_specification

// Upper limit of the session command history. This will never be set as the buffer ID for a query which means
// the range of possible values are from 1 to UINT32_MAX - 1.
const uint32_t MAX_SESCMD_ID = std::numeric_limits<uint32_t>::max();

// A helper class that writes a CancelRequest packet into a TCP socket and then closes it.
class CancelRequest : public mxb::Pollable
{
public:
    CancelRequest(int fd, uint32_t pid, uint32_t secret)
        : m_fd(fd)
    {
        auto ptr = m_data.data();
        pg::set_uint32(ptr, 16);
        pg::set_uint32(ptr + 4, pg::CANCEL_MAGIC);
        pg::set_uint32(ptr + 8, pid);
        pg::set_uint32(ptr + 12, secret);

        m_it = m_data.begin();
    }

    int poll_fd() const override
    {
        return m_fd;
    }
    uint32_t handle_poll_events(mxb::Worker* pWorker, uint32_t events, Context context) override
    {
        bool stop = false;

        if (events & EPOLLOUT)
        {
            int rc = write(m_fd, m_it, std::distance(m_it, m_data.end()));

            if (rc >= 0)
            {
                m_it += rc;

                if (m_it == m_data.end())
                {
                    stop = true;
                }
            }
            else if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                MXB_INFO("Failed to write CancelRequest: %d, %s", errno, mxb_strerror(errno));
                stop = true;
            }
        }
        else if (events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
        {
            MXB_INFO("Network error event during CancelRequest handling");
            stop = true;
        }

        if (stop)
        {
            pWorker->remove_pollable(this);
            delete this;
        }

        return events;
    }

    ~CancelRequest()
    {
        close(m_fd);
    }

private:
    int                     m_fd;
    std::array<uint8_t, 16> m_data;
    uint8_t*                m_it;
};

void add_packet_auth_request(GWBUF& gwbuf, pg::Auth athentication_method)
{
    const size_t auth_len = 1 + 4 + 4;      // Byte1('R'), Int32(8) len, Int32 auth_method
    std::array<uint8_t, auth_len> data;

    uint8_t* ptr = begin(data);
    *ptr++ = pg::AUTHENTICATION;
    ptr += pg::set_uint32(ptr, 8);
    ptr += pg::set_uint32(ptr, athentication_method);

    gwbuf.append(begin(data), data.size());
}

void add_packet_keydata(GWBUF& gwbuf, uint32_t id, uint32_t key)
{
    const size_t auth_len = 1   // Byte1('K')
        + 4                     // Int32(12) len
        + 4                     // Int32 PID (session ID in maxscale)
        + 4;                    // Int32 The "secret" key
    std::array<uint8_t, auth_len> data;

    uint8_t* ptr = begin(data);
    *ptr++ = pg::BACKEND_KEY_DATA;
    ptr += pg::set_uint32(ptr, 12);
    ptr += pg::set_uint32(ptr, id);
    ptr += pg::set_uint32(ptr, key);

    gwbuf.append(begin(data), data.size());
}

void add_packet_parameter_status(GWBUF& gwbuf, std::string_view key, std::string_view value)
{
    const size_t len = 1    // Byte1('S')
        + 4                 // Int32 len
        + key.size() + 1    // String name
        + value.size() + 1; // String status

    auto [ptr, _] = gwbuf.prepare_to_write(len);
    *ptr++ = pg::PARAMETER_STATUS;
    ptr += pg::set_uint32(ptr, len - 1);    // Exclude the command byte
    ptr += pg::set_string(ptr, key);
    ptr += pg::set_string(ptr, value);
    gwbuf.write_complete(len);
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

PgClientConnection::PgClientConnection(MXS_SESSION* pSession,
                                       mxs::Parser* pParser,
                                       mxs::Component* pComponent,
                                       const UserAuthSettings& auth_settings)
    : m_session(*pSession)
    , m_parser(*pParser)
    , m_ssl_required(m_session.listener_data()->m_ssl.config().enabled)
    , m_down(pComponent)
    , m_protocol_data(static_cast<PgProtocolData*>(pSession->protocol_data()))
    , m_user_auth_settings(auth_settings)
    , m_qc(m_parser, pSession)
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

    if (auto [ok, gwbuf] = pg::read_packet(m_dcb, expect); ok)
    {
        if (gwbuf)
        {
            switch (m_state)
            {
            case State::INIT:
                m_state = state_init(gwbuf);
                break;

            case State::AUTH:
                m_state = state_auth(std::move(gwbuf));
                break;

            case State::ROUTE:
                m_state = state_route(std::move(gwbuf));
                break;

            case State::WAIT_USERDATA:
                // Should not get client data (or read events) before users have actually been updated.
                // Perhaps this can happen with buggy clients?
                MXB_ERROR("Client %s sent data when waiting for user account update. Closing session.",
                          m_session.user_and_host().c_str());
                send_error("08P01", "Unexpected client event");     // 08P01 = protocol violation
                m_session.service->unmark_for_wakeup(this);
                m_state = State::ERROR;
                // Pass to error below.

            case State::ERROR:
                // pass, handled below
                break;
            }

            // Only trigger when a completed packet was read to prevent infinite triggering.
            if (m_state != State::ERROR && !m_dcb->readq_empty())
            {
                m_dcb->trigger_read_event();
            }
        }
    }
    else
    {
        m_state = State::ERROR;
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
    else if (gwbuf.length() == 16 && first_word == pg::CANCEL_MAGIC)
    {
        uint32_t id = pg::get_uint32(gwbuf.data() + 8);
        uint32_t secret = pg::get_uint32(gwbuf.data() + 12);
        MXB_INFO("CancelRequest for session %u with secret %u.", id, secret);
        send_cancel_request(id, secret);

        // Technically this is not an error but treating it as one is OK since no response is sent.
        next_state = State::ERROR;
    }
    else if (parse_startup_message(gwbuf))
    {
        if (update_user_account_entry())
        {
            next_state = prepare_auth();
        }
        else
        {
            // User data may be outdated, send update message through the service.
            // The current session will stall until userdata has been updated.
            m_session.service->request_user_account_update();
            m_session.service->mark_for_wakeup(this);
            next_state = State::WAIT_USERDATA;
        }
    }

    return next_state;
}

PgClientConnection::State PgClientConnection::prepare_auth()
{
    State next_state = State::ERROR;
    if (m_authenticator)
    {
        auto pw_request_packet = m_authenticator->authentication_request();
        if (pw_request_packet.empty())
        {
            // The user is trusted, no authentication necessary.
            auto entry_type = m_protocol_data->auth_data().user_entry.type;
            if (entry_type == UserEntryType::USER_ACCOUNT_OK)
            {
                if (check_allow_login())
                {
                    next_state = start_session() ? State::ROUTE : State::ERROR;
                }
            }
            else
            {
                mxb_assert(entry_type == UserEntryType::NO_AUTH_ID_ENTRY);
                send_error(invalid_auth, mxb::string_printf("role \"%s\" does not exist",
                                                            m_session.user().c_str()));
            }
        }
        else
        {
            m_dcb->writeq_append(std::move(pw_request_packet));
            next_state = State::AUTH;
        }
    }
    else
    {
        // Either user account did not match or auth method is not enabled.
        const char* enc = (m_dcb->ssl_state() == DCB::SSLState::ESTABLISHED) ? "SSL encryption" :
            "no encryption";
        string msg = mxb::string_printf(
            "no pg_hba.conf entry for host \"%s\", user \"%s\", database \"%s\", %s",
            m_session.client_remote().c_str(), m_session.user().c_str(),
            m_protocol_data->default_db().c_str(), enc);
        send_error(invalid_auth, msg);
    }
    return next_state;
}

PgClientConnection::State PgClientConnection::state_auth(GWBUF&& packet)
{
    using ExchRes = PgClientAuthenticator::ExchRes;
    using AuthRes = PgClientAuthenticator::AuthRes;

    enum class Result {READY, CONTINUE, ERROR};
    auto result = Result::ERROR;

    auto res = m_authenticator->exchange(std::move(packet), *m_protocol_data);
    if (!res.packet.empty())
    {
        m_dcb->writeq_append(std::move(res.packet));
    }

    switch (res.status)
    {
    case ExchRes::Status::READY:
        {
            // If user didn't have a proper auth_id entry, fail right away.
            const auto& user_entry = m_protocol_data->auth_data().user_entry;
            if (user_entry.type == UserEntryType::USER_ACCOUNT_OK)
            {
                AuthRes auth_res;
                if (m_user_auth_settings.check_password)
                {
                    auth_res = m_authenticator->authenticate(*m_protocol_data);
                }
                else
                {
                    auth_res.status = AuthRes::Status::SUCCESS;
                    result = Result::READY;
                }

                if (auth_res.status == AuthRes::Status::SUCCESS)
                {
                    if (check_allow_login())
                    {
                        result = Result::READY;
                    }
                }
                else
                {
                    if (auth_res.status == AuthRes::Status::FAIL_WRONG_PW
                        && user_account_cache()->can_update_immediately())
                    {
                        // Again, this may be because user data is obsolete. Update userdata, but fail
                        // session anyway since I/O with client cannot be redone.
                        m_session.service->request_user_account_update();
                    }
                    send_error("28P01", mxb::string_printf("password authentication failed for user \"%s\"",
                                                           m_session.user().c_str()));
                }
            }
            else
            {
                mxb_assert(user_entry.type == UserEntryType::NO_AUTH_ID_ENTRY);
                send_error(invalid_auth, mxb::string_printf("role \"%s\" does not exist",
                                                            m_session.user().c_str()));
            }
        }
        break;

    case ExchRes::Status::INCOMPLETE:
        result = Result::CONTINUE;
        break;

    case ExchRes::Status::FAIL:
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

        // The random "secret" is used when the connection is killed and it must match the value we generate
        // here. This is because the Postgres protocol allows killing connections without a need to
        // authenticate the user who's doing the killing. As the secret is either sent in plaintext, in which
        // case it's not really a secret, or over TLS, it doesn't need to be from a cryptographically secure
        // pseudorandom number generate.
        m_session.worker()->gen_random_bytes(reinterpret_cast<uint8_t*>(&m_secret), sizeof(m_secret));
        add_packet_keydata(rdy, m_session.id(), m_secret);

        // TODO: Probe the hard-coded values from the server
        add_packet_parameter_status(rdy, "client_encoding", m_protocol_data->client_encoding());
        add_packet_parameter_status(rdy, "application_name", m_protocol_data->application_name());
        add_packet_parameter_status(rdy, "session_authorization", m_session.user());
        add_packet_parameter_status(rdy, "DateStyle", "ISO, MDY");
        add_packet_parameter_status(rdy, "server_encoding", "UTF8");
        add_packet_parameter_status(rdy, "standard_conforming_strings", "on");
        add_packet_parameter_status(rdy, "server_version", m_session.service->version_string());
        add_packet_parameter_status(rdy, "default_transaction_read_only", "off");
        add_packet_parameter_status(rdy, "in_hot_standby", "off");
        add_packet_parameter_status(rdy, "is_superuser", "on");

        add_packet_ready_for_query(rdy);
        write(std::move(rdy));
        rval = true;
    }
    else
    {
        send_error("XX000", "Internal error: Session creation failed");
        MXB_ERROR("Failed to create session for %s.", m_session.user_and_host().c_str());
    }
    return rval;
}

PgClientConnection::State PgClientConnection::state_wait_userdata()
{
    MXB_AT_DEBUG(bool ret = ) update_user_account_entry();
    mxb_assert(ret);
    return prepare_auth();
}

PgClientConnection::State PgClientConnection::state_route(GWBUF&& gwbuf)
{
    uint8_t cmd = gwbuf[0];

    switch (cmd)
    {
    case pg::TERMINATE:
        m_session.set_normal_quit();
        m_session.set_can_pool_backends(true);
        break;

    case pg::QUERY:
        if (!record_for_history(gwbuf))
        {
            // Wasn't recorded in the history, treat as a simple request.
            m_requests.push_back(SimpleRequest {});
        }
        break;

    case pg::PARSE:
        record_parse_for_history(gwbuf);
        break;

    default:
        if (pg::will_respond(cmd))
        {
            m_requests.push_back(SimpleRequest {});
        }
        break;
    }

    if (!m_down->routeQuery(std::move(gwbuf)))
    {
        m_state = State::ERROR;
        m_session.kill();
    }

    return State::ROUTE;
}

void PgClientConnection::error(DCB* dcb, const char* errmsg)
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
    if (m_state == State::WAIT_USERDATA)
    {
        m_session.service->unmark_for_wakeup(this);
    }
}

bool PgClientConnection::clientReply(GWBUF&& buffer,
                                     const mxs::ReplyRoute& down,
                                     const mxs::Reply& reply)
{
    if (reply.is_complete())
    {
        if (!m_requests.empty())
        {
            auto visitor = [&](auto&& response){
                handle_response(std::move(response), reply);
            };

            std::visit(visitor, std::move(m_requests.front()));
            m_requests.erase(m_requests.begin());
        }
        else
        {
            m_session.kill();
            mxb_assert_message(!true, "Unexpected response");
        }

        if (m_session.capabilities() & RCAP_TYPE_SESCMD_HISTORY)
        {
            m_qc.update_from_reply(reply);
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
    return &m_parser;
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
    string_view app_name;
    string_view client_encoding = "UTF8";   // TODO: Probe this from the backend servers
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
                else if (param_name == "application_name")
                {
                    app_name = param_value;
                }
                else if (param_name == "client_encoding")
                {
                    client_encoding = param_value;
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
            m_protocol_data->set_application_name(app_name);
            m_protocol_data->set_client_encoding(client_encoding);
            m_protocol_data->set_connect_params(params_begin, end);
            rval = true;
        }
    }
    return rval;
}

/**
 * @return True if client connection should continue with authentication. False, if it should wait for
 * user account update.
 */
bool PgClientConnection::update_user_account_entry()
{
    auto& auth_data = m_protocol_data->auth_data();
    const bool first_try = auth_data.user_entry.type == UserEntryType::UNKNOWN;
    auto& ses = m_session;
    auto match_host = m_user_auth_settings.match_host_pattern ? PgUserCache::MatchHost::YES :
        PgUserCache::MatchHost::NO;

    auto set_authenticator = [&](PgAuthenticatorModule* auth_module) {
        auto& entry = auth_data.user_entry;
        mxb_assert(entry.type == UserEntryType::USER_ACCOUNT_OK
                   || entry.type == UserEntryType::NO_AUTH_ID_ENTRY);
        if (auth_module)
        {
            // Correct plugin is loaded, generate session-specific data.
            MXB_INFO("Client %s matched pg_hba.conf entry at line %i.", ses.user_and_host().c_str(),
                     entry.line_no);
            m_authenticator = auth_module->create_client_authenticator();
            auth_data.auth_module = auth_module;
            auth_data.user = ses.user();
        }
        else
        {
            // Authentication cannot continue in this case.
            entry.type = UserEntryType::METHOD_NOT_SUPPORTED;
            MXB_INFO("Client %s matched pg_hba.conf entry at line %i. Entry uses unsupported authentication "
                     "method '%s'. Cannot authenticate user.",
                     ses.user_and_host().c_str(), entry.line_no, entry.auth_method.c_str());
        }
    };

    auto hba_entry_found = [](UserEntryType type) {
        return type == UserEntryType::USER_ACCOUNT_OK || type == UserEntryType::NO_AUTH_ID_ENTRY;
    };
    bool proceed = false;

    if (first_try)
    {
        // On the first try, find the user entry and see if it's perfect (no clear reason for failed auth).
        // If the entry is not ideal, try to reload users if possible.
        auto* user_cache = user_account_cache();
        m_protocol_data->set_user_entry(user_cache->find_user(
            ses.user(), ses.client_remote(), m_protocol_data->default_db(), match_host));
        m_orig_userdb_version = user_cache->version();

        PgAuthenticatorModule* auth_module = nullptr;
        bool auth_module_search = false;
        const auto& entry = auth_data.user_entry;

        if (entry.type == UserEntryType::USER_ACCOUNT_OK && entry.authid_entry.can_login)
        {
            auth_module = find_auth_module(entry.auth_method);
            auth_module_search = true;
            if (auth_module)
            {
                // Looks good, lock in the current userdata, don't try to reload.
                set_authenticator(auth_module);
                proceed = true;
            }
        }

        if (!proceed)
        {
            if (user_cache->can_update_immediately())
            {
                // Return false, wait for user data update.
            }
            else
            {
                MXB_WARNING(PgUserManager::RECENTLY_UPDATED_FMT, m_session.user_and_host().c_str());
                // Can't update, have to make do with what we have. Authentication will fail, although the
                // client may not find out just yet.
                if (hba_entry_found(entry.type))
                {
                    if (!auth_module_search)
                    {
                        auth_module = find_auth_module(entry.auth_method);
                    }
                    set_authenticator(auth_module);
                }
                proceed = true;
            }
        }
    }
    else
    {
        // Second try. If data changed, search again. Otherwise, use previous data. A search can be
        // expensive if there are many entries or DNS is involved.
        if (user_account_cache()->version() > m_orig_userdb_version)
        {
            m_protocol_data->set_user_entry(user_account_cache()->find_user(
                ses.user(), ses.client_remote(), m_protocol_data->default_db(), match_host));
        }

        mxb_assert(auth_data.user_entry.type != UserEntryType::UNKNOWN);
        if (hba_entry_found(auth_data.user_entry.type))
        {
            set_authenticator(find_auth_module(auth_data.user_entry.auth_method));
        }
        proceed = true;
    }
    return proceed;
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

void PgClientConnection::send_error(string_view sqlstate, string_view msg)
{
    m_dcb->writeq_append(pg::make_error(pg::Severity::FATAL, sqlstate, msg));
}

bool PgClientConnection::check_allow_login()
{
    bool rval = false;
    auto& user_entry = m_protocol_data->auth_data().user_entry;
    if (user_entry.authid_entry.can_login)
    {
        if (user_entry.authid_entry.super && mxs::Config::get().log_warn_super_user)
        {
            MXB_WARNING("Super user %s logged in to service '%s'.",
                        m_session.user_and_host().c_str(), m_session.service->name());
        }
        rval = true;
    }
    else
    {
        send_error(invalid_auth, mxb::string_printf("role \"%s\" is not permitted to log in",
                                                    m_session.user().c_str()));
    }
    return rval;
}

bool PgClientConnection::record_for_history(GWBUF& buffer)
{
    bool recorded = false;

    if (m_session.capabilities() & RCAP_TYPE_SESCMD_HISTORY)
    {
        // Update the routing information. This must be done even if the command isn't added to the history.
        const auto& info = m_qc.update_and_commit_route_info(buffer);

        if (m_qc.target_is_all(info.target()))
        {
            // We need to record this response in the history
            buffer.set_id(m_next_id);
            m_requests.push_back(HistoryRequest {std::make_unique<GWBUF>(buffer.deep_clone())});

            if (++m_next_id == MAX_SESCMD_ID)
            {
                m_next_id = 1;
            }

            recorded = true;
        }
    }

    return recorded;
}

void PgClientConnection::record_parse_for_history(GWBUF& buffer)
{
    if (m_session.capabilities() & RCAP_TYPE_SESCMD_HISTORY)
    {
        buffer.set_id(m_next_id);

        // We need to record the Parse in the history. Since the Parse command does not generate a response on
        // its own, we need to add a Sync packet after it to "commit" the batch of extended query operations.
        // This'll be handled transparently by the history replay since it expects one response per executed
        // "session command". An optimization would be to batch the parse commands and send only one Sync
        // command.
        constexpr uint8_t sync_packet[] = {'S', 0, 0, 0, 4};
        GWBUF tmp = buffer.deep_clone();
        tmp.append(sync_packet, sizeof(sync_packet));

        m_requests.push_back(HistoryRequest {std::make_unique<GWBUF>(std::move(tmp))});

        if (++m_next_id == MAX_SESCMD_ID)
        {
            m_next_id = 1;
        }
    }
}

void PgClientConnection::handle_response(SimpleRequest&& req, const mxs::Reply& reply)
{
    if (auto trx_state = reply.get_variable(pg::TRX_STATE_VARIABLE); !trx_state.empty())
    {
        // If the value is anything other than 'I', a transaction is open.
        m_protocol_data->set_in_trx(trx_state[0] != 'I');
    }
}

void PgClientConnection::handle_response(HistoryRequest&& req, const mxs::Reply& reply)
{
    mxb_assert(m_session.capabilities() & RCAP_TYPE_SESCMD_HISTORY);

    // Check the history responses once we've returned from clientReply
    m_session.worker()->lcall([this, id = req->id(), ok = !reply.error()](){
        if (m_session.is_alive())
        m_protocol_data->history().check_early_responses(id, ok);
    });

    m_protocol_data->history().add(std::move(*req), !reply.error());
}

void PgClientConnection::send_cancel_request(uint32_t id, uint32_t secret)
{
    mxs::RoutingWorker::broadcast([secret, id](){
        if (auto ses = find_matching_session(id, secret))
        {
            MXS_SESSION::Scope scope(ses);

            for (mxs::BackendConnection* b : ses->backend_connections())
            {
                auto p = static_cast<PgBackendConnection*>(b);
                SERVER* srv = b->dcb()->server();
                MXB_INFO("Sending CancelRequest to '%s'", srv->name());
                struct sockaddr_storage addr {};

                if (int fd = srv->connect_socket(&addr); fd != -1)
                {
                    // We're not expecting any EPOLLIN events
                    constexpr uint32_t poll_events = EPOLLOUT | EPOLLHUP | EPOLLRDHUP | EPOLLET;
                    CancelRequest* req = new CancelRequest(fd, p->pid(), p->secret());

                    if (!mxs::RoutingWorker::get_current()->add_pollable(poll_events, req))
                    {
                        delete req;
                    }
                }
            }
        }
    }, mxs::RoutingWorker::EXECUTE_AUTO);
}

MXS_SESSION* PgClientConnection::find_matching_session(uint32_t id, uint32_t secret)
{
    MXS_SESSION* rv = nullptr;
    const auto& registry = mxs::RoutingWorker::get_current()->session_registry();

    // If we haven't created enough sessions to overflow the 32-bit unsigned int range, a single
    // lookup into the registry will tell us if the session is there.
    MXS_SESSION* session = registry.lookup(id);

    if (session && session->protocol()->name() == MXS_POSTGRESQL_PROTOCOL_NAME)
    {
        auto client = static_cast<PgClientConnection*>(session->client_connection());

        if (client->m_secret == secret)
        {
            rv = session;
        }
    }
    // Otherwise, we'll have to iterate over the whole registry to see if any of the sessions match
    else if (session_max_id() > std::numeric_limits<uint32_t>::max())
    {
        for (auto [cand_id, candidate] : registry)
        {
            if ((uint32_t)cand_id == id && candidate->protocol()->name() == MXS_POSTGRESQL_PROTOCOL_NAME)
            {
                auto client = static_cast<PgClientConnection*>(candidate->client_connection());

                if (client->m_secret == secret)
                {
                    rv = candidate;
                    break;
                }
            }
        }
    }

    return rv;
}

void PgClientConnection::wakeup()
{
    mxb_assert(m_state == State::WAIT_USERDATA);
    m_state = state_wait_userdata();
    if (m_state == State::ERROR)
    {
        m_session.kill();
    }
}
