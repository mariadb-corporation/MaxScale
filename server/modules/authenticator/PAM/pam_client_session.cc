/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_client_session.hh"

#include <set>
#include <maxbase/externcmd.hh>
#include <maxbase/pam_utils.hh>
#include <maxscale/protocol/mariadb/client_connection.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include "pam_instance.hh"

using maxscale::Buffer;
using std::string;
using std::string_view;
using AuthRes = mariadb::ClientAuthenticator::AuthRes;
using ExchRes = mariadb::ClientAuthenticator::ExchRes;
using mariadb::UserEntry;

namespace
{
const char unexpected_state[] = "Unexpected authentication state: %d";

/**
 * Read the client's password, store it to buffer.
 *
 * @param buffer Buffer containing the password
 * @param out Password output
 * @return True on success, false if packet didn't have a valid header
 */
bool store_client_password(const GWBUF& buffer, mariadb::AuthByteVec& out)
{
    bool rval = false;
    if (buffer.length() >= MYSQL_HEADER_LEN)
    {
        size_t plen = mariadb::get_header(buffer.data()).pl_length;
        out.resize(plen);
        buffer.copy_data(MYSQL_HEADER_LEN, plen, out.data());
        rval = true;
    }
    return rval;
}

string_view eff_pam_service(string_view pam_service)
{
    return pam_service.empty() ? "mysql" : pam_service;
}
}

PamClientAuthenticator::PamClientAuthenticator(AuthSettings settings, const PasswordMap& backend_pwds,
                                               MariaDBClientConnection& client,
                                               std::unique_ptr<mxb::AsyncProcess> proc)
    : m_settings(settings)
    , m_backend_pwds(backend_pwds)
    , m_client(client)
    , m_proc(std::move(proc))
{
}

/**
 * @brief Create an AuthSwitchRequest packet
 *
 * The server (MaxScale) sends the plugin name "dialog" to the client with the
 * first password prompt. We want to avoid calling the PAM conversation function
 * more than once because it blocks, so we "emulate" its behaviour here.
 * This obviously only works with the basic password authentication scheme.
 *
 * @return Allocated packet
 * @see
 * https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::AuthSwitchRequest
 */
GWBUF PamClientAuthenticator::create_auth_change_packet(std::string_view msg) const
{
    bool dialog = !m_settings.cleartext_plugin;
    /**
     * The AuthSwitchRequest packet:
     * 4 bytes     - Header
     * 0xfe        - Command byte
     * string[NUL] - Auth plugin name
     * byte        - Message type (contained in msg)
     * string[EOF] - Message (contained in msg)
     *
     * If using mysql_clear_password, no message is added.
     */
    size_t plen = dialog ? (1 + DIALOG_SIZE + msg.length()) : (1 + CLEAR_PW_SIZE);
    size_t buflen = MYSQL_HEADER_LEN + plen;
    GWBUF rval(buflen);
    uint8_t* pData = mariadb::write_header(rval.data(), plen, 0);
    *pData++ = MYSQL_REPLY_AUTHSWITCHREQUEST;
    if (dialog)
    {
        pData = mariadb::copy_chars(pData, DIALOG.c_str(), DIALOG_SIZE);// Plugin name.
        mariadb::copy_chars(pData, msg.data(), msg.size());             // Message type + contents
    }
    else
    {
        memcpy(pData, CLEAR_PW.c_str(), CLEAR_PW_SIZE);
    }
    return rval;
}

mariadb::ClientAuthenticator::ExchRes
PamClientAuthenticator::exchange(GWBUF&& buffer, MYSQL_session* session, AuthenticationData& auth_data)
{
    return (m_settings.mode == AuthMode::SUID) ? exchange_suid(std::move(buffer), session, auth_data) :
           exchange_old(std::move(buffer), session, auth_data);
}

mariadb::ClientAuthenticator::ExchRes
PamClientAuthenticator::exchange_old(GWBUF&& buffer, MYSQL_session* session, AuthenticationData& auth_data)
{
    ExchRes rval;

    switch (m_state)
    {
    case State::INIT:
        {
            int msglen = 1 + PASSWORD_QUERY.length();
            char msg[msglen];
            msg[0] = DIALOG_ECHO_DISABLED;
            memcpy(msg + 1, PASSWORD_QUERY.c_str(), PASSWORD_QUERY.length());
            rval.packet = create_auth_change_packet(string_view(msg, msglen));
            rval.status = ExchRes::Status::INCOMPLETE;
            m_state = State::ASKED_FOR_PW;
        }
        break;

    case State::ASKED_FOR_PW:
        // Client should have responded with password.
        if (store_client_password(buffer, auth_data.client_token))
        {
            if (m_settings.mode == AuthMode::PW)
            {
                rval.status = ExchRes::Status::READY;
                m_state = State::PW_RECEIVED;
            }
            else
            {
                // Generate prompt for 2FA.
                int msglen = 1 + TWO_FA_QUERY.length();
                char msg[msglen];
                msg[0] = DIALOG_ECHO_DISABLED;      // Equivalent to server 2FA prompt
                memcpy(msg + 1, TWO_FA_QUERY.c_str(), TWO_FA_QUERY.length());
                rval.packet = create_2fa_prompt_packet(string_view(msg, msglen));
                rval.status = ExchRes::Status::INCOMPLETE;
                m_state = State::ASKED_FOR_2FA;
            }
        }
        break;

    case State::ASKED_FOR_2FA:
        if (store_client_password(buffer, auth_data.client_token_2fa))
        {
            rval.status = ExchRes::Status::READY;
            m_state = State::PW_RECEIVED;
        }
        break;

    default:
        MXB_ERROR(unexpected_state, static_cast<int>(m_state));
        mxb_assert(!true);
        break;
    }
    return rval;
}

mariadb::ClientAuthenticator::ExchRes
PamClientAuthenticator::exchange_suid(GWBUF&& buffer, MYSQL_session* session, AuthenticationData& auth_data)
{
    ExchRes rval;
    // TODO
    return rval;
}

AuthRes PamClientAuthenticator::authenticate(MYSQL_session* session, AuthenticationData& auth_data)
{
    return (m_settings.mode == AuthMode::SUID) ? authenticate_suid(session, auth_data) :
           authenticate_old(session, auth_data);
}

AuthRes PamClientAuthenticator::authenticate_old(MYSQL_session* session, AuthenticationData& auth_data)
{
    using mxb::pam::AuthResult;
    mxb_assert(m_state == State::PW_RECEIVED);
    bool twofa = (m_settings.mode == AuthMode::PW_2FA);
    bool map_to_mariadbauth = (m_settings.be_mapping == BackendMapping::MARIADB);
    const auto& entry = auth_data.user_entry.entry;

    /** We sent the authentication change packet + plugin name and the client
     * responded with the password. Try to continue authentication without more
     * messages to client. */

    const auto& tok1 = auth_data.client_token;
    const auto& tok2 = auth_data.client_token_2fa;
    const auto& user_name = auth_data.user;

    // Take username from the session object, not the user entry. The entry may be anonymous.
    mxb::pam::UserData user = {user_name, session->remote};
    mxb::pam::PwdData pwds;
    pwds.password.assign((const char*)tok1.data(), tok1.size());
    if (twofa)
    {
        pwds.two_fa_code.assign((const char*)tok2.data(), tok2.size());
    }
    mxb::pam::ExpectedMsgs expected_msgs = {mxb::pam::EXP_PW_QUERY, ""};

    // The server PAM plugin uses "mysql" as the default service when authenticating
    // a user with no service.
    mxb::pam::AuthSettings sett;
    sett.service = eff_pam_service(entry.auth_string);
    sett.mapping_on = map_to_mariadbauth;

    AuthRes rval;
    AuthResult res = mxb::pam::authenticate(m_settings.mode, user, pwds, sett, expected_msgs);
    if (res.type == AuthResult::Result::SUCCESS)
    {
        rval.status = AuthRes::Status::SUCCESS;
        // Don't copy auth tokens when mapping is on so that backend authenticator will try to authenticate
        // without a password.
        if (!map_to_mariadbauth)
        {
            auth_data.backend_token = tok1;
            if (twofa)
            {
                auth_data.backend_token_2fa = tok2;
            }
        }

        if (map_to_mariadbauth && !res.mapped_user.empty())
        {
            if (res.mapped_user != user_name)
            {
                MXB_INFO("Incoming user '%s' mapped to '%s'.",
                         user_name.c_str(), res.mapped_user.c_str());
                auth_data.user = res.mapped_user;   // TODO: Think if using a separate field would be better.
                // If a password for the user is found in the passwords map, use that. Otherwise, try
                // passwordless authentication.
                const auto& it = m_backend_pwds.find(res.mapped_user);
                if (it != m_backend_pwds.end())
                {
                    MXB_INFO("Using password found in backend passwords file for '%s'.",
                             res.mapped_user.c_str());
                    auto begin = it->second.pw_hash;
                    auto end = begin + SHA_DIGEST_LENGTH;
                    auth_data.backend_token.assign(begin, end);
                }
            }
        }
    }
    else
    {
        if (res.type == AuthResult::Result::WRONG_USER_PW)
        {
            rval.status = AuthRes::Status::FAIL_WRONG_PW;
        }
        rval.msg = res.error;
    }

    m_state = State::DONE;
    return rval;
}

AuthRes PamClientAuthenticator::authenticate_suid(MYSQL_session* session, AuthenticationData& auth_data)
{
    AuthRes rval;
    // TODO
    return rval;
}

GWBUF PamClientAuthenticator::create_2fa_prompt_packet(std::string_view msg) const
{
    /**
     * 4 bytes     - Header
     * byte        - Message type (contained in msg)
     * string[EOF] - Message (contained in msg)
     */
    size_t plen = msg.size();
    size_t buflen = MYSQL_HEADER_LEN + plen;
    GWBUF rval(buflen);
    uint8_t* pData = mariadb::write_header(rval.data(), plen, 0);
    mariadb::copy_chars(pData, msg.data(), msg.length());
    return rval;
}

PipeWatcher::PipeWatcher(MariaDBClientConnection& client, mxb::Worker* worker, int fd)
    : m_client(client)
    , m_worker(worker)
    , m_poll_fd(fd)
{
}

int PipeWatcher::poll_fd() const
{
    return m_poll_fd;
}

uint32_t
PipeWatcher::handle_poll_events(mxb::Worker* pWorker, uint32_t events, maxbase::Pollable::Context context)
{
    // Any error or hangup events will be detected when reading from the pipe.
    m_client.trigger_ext_auth_exchange();
    // At this point, the "this" object may be already deleted. Don't access any fields.
    return events;
}

bool PipeWatcher::poll()
{
    mxb_assert(!m_polling);
    auto ret = m_worker->add_pollable(EPOLLIN, this);
    if (ret)
    {
        m_polling = true;
    }
    return ret;
}

bool PipeWatcher::stop_poll()
{
    mxb_assert(m_polling);
    auto ret = m_worker->remove_pollable(this);
    if (ret)
    {
        m_polling = false;
    }
    return ret;
}

PipeWatcher::~PipeWatcher()
{
    if (m_polling)
    {
        stop_poll();
    }
}
