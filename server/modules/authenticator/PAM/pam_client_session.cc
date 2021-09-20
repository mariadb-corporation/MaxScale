/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_client_session.hh"

#include <set>
#include <maxbase/pam_utils.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include "pam_instance.hh"

using maxscale::Buffer;
using std::string;
using AuthRes = mariadb::ClientAuthenticator::AuthRes;
using mariadb::UserEntry;

namespace
{

/**
 * Read the client's password, store it to buffer.
 *
 * @param buffer Buffer containing the password
 * @param output Password output
 * @return True on success, false if packet didn't have a valid header
 */
bool store_client_password(GWBUF* buffer, mariadb::ClientAuthenticator::ByteVec* output)
{
    bool rval = false;
    uint8_t header[MYSQL_HEADER_LEN];

    if (gwbuf_copy_data(buffer, 0, MYSQL_HEADER_LEN, header) == MYSQL_HEADER_LEN)
    {
        size_t plen = mariadb::get_byte3(header);
        output->resize(plen);
        gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, plen, output->data());
        rval = true;
    }
    return rval;
}
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
Buffer PamClientAuthenticator::create_auth_change_packet() const
{
    bool dialog = !m_cleartext_plugin;
    /**
     * The AuthSwitchRequest packet:
     * 4 bytes     - Header
     * 0xfe        - Command byte
     * string[NUL] - Auth plugin name
     * byte        - Message type
     * string[EOF] - Message
     *
     * If using mysql_clear_password, no messages are added.
     */
    size_t plen = dialog ? (1 + DIALOG_SIZE + 1 + PASSWORD_QUERY.length()) : (1 + CLEAR_PW_SIZE);
    size_t buflen = MYSQL_HEADER_LEN + plen;
    uint8_t bufdata[buflen];
    uint8_t* pData = bufdata;
    mariadb::set_byte3(pData, plen);
    pData += 3;
    *pData++ = m_sequence;
    *pData++ = MYSQL_REPLY_AUTHSWITCHREQUEST;
    if (dialog)
    {
        memcpy(pData, DIALOG.c_str(), DIALOG_SIZE);     // Plugin name.
        pData += DIALOG_SIZE;
        *pData++ = DIALOG_ECHO_DISABLED;
        memcpy(pData, PASSWORD_QUERY.c_str(), PASSWORD_QUERY.length());     // First message
    }
    else
    {
        memcpy(pData, CLEAR_PW.c_str(), CLEAR_PW_SIZE);
    }

    Buffer buffer(bufdata, buflen);
    return buffer;
}

mariadb::ClientAuthenticator::ExchRes
PamClientAuthenticator::exchange(GWBUF* buffer, MYSQL_session* session, mxs::Buffer* output_packet)
{
    m_sequence = session->next_sequence;
    auto rval = ExchRes::FAIL;

    switch (m_state)
    {
    case State::INIT:
        {
            // TODO: what if authenticator was already correct? Could this part be skipped?
            Buffer authbuf = create_auth_change_packet();
            if (authbuf.length())
            {
                m_state = State::ASKED_FOR_PW;
                *output_packet = std::move(authbuf);
                rval = ExchRes::INCOMPLETE;
            }
        }
        break;

    case State::ASKED_FOR_PW:
        // Client should have responded with password.
        if (store_client_password(buffer, &session->client_token))
        {
            if (m_mode == AuthMode::PW)
            {
                m_state = State::PW_RECEIVED;
                rval = ExchRes::READY;
            }
            else
            {
                // Generate prompt for 2FA.
                Buffer prompt = create_2fa_prompt_packet();
                *output_packet = std::move(prompt);
                m_state = State::ASKED_FOR_2FA;
                rval = ExchRes::INCOMPLETE;
            }
        }
        break;

    case State::ASKED_FOR_2FA:
        if (store_client_password(buffer, &session->client_token_2fa))
        {
            m_state = State::PW_RECEIVED;
            rval = ExchRes::READY;
        }
        break;

    default:
        MXS_ERROR("Unexpected authentication state: %d", static_cast<int>(m_state));
        mxb_assert(!true);
        break;
    }
    return rval;
}

AuthRes PamClientAuthenticator::authenticate(const UserEntry* entry, MYSQL_session* session)
{
    using mxb::pam::AuthResult;
    AuthRes rval;
    mxb_assert(m_state == State::PW_RECEIVED);
    bool twofa = (m_mode == AuthMode::PW_2FA);
    bool map_to_mariadbauth = (m_be_mapping == BackendMapping::MARIADB);

    /** We sent the authentication change packet + plugin name and the client
     * responded with the password. Try to continue authentication without more
     * messages to client. */

    const auto& tok1 = session->client_token;
    const auto& tok2 = session->client_token_2fa;

    // Take username from the session object, not the user entry. The entry may be anonymous.
    mxb::pam::UserData user = {session->user, session->remote};
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
    sett.service = entry->auth_string.empty() ? "mysql" : entry->auth_string;
    sett.mapping_on = map_to_mariadbauth;

    AuthResult res = mxb::pam::authenticate(m_mode, user, pwds, sett, expected_msgs);
    if (res.type == AuthResult::Result::SUCCESS)
    {
        rval.status = AuthRes::Status::SUCCESS;
        // Don't copy auth tokens when mapping is on so that backend authenticator will try to authenticate
        // without a password.
        if (!map_to_mariadbauth)
        {
            session->backend_token = tok1;
            if (twofa)
            {
                session->backend_token_2fa = tok2;
            }
        }

        if (map_to_mariadbauth && !res.mapped_user.empty())
        {
            if (res.mapped_user != session->user)
            {
                MXB_INFO("Incoming user '%s' mapped to '%s'.",
                         session->user.c_str(), res.mapped_user.c_str());
                session->user = res.mapped_user;    // TODO: Think if using a separate field would be better.
                // If a password for the user is found in the passwords map, use that. Otherwise, try
                // passwordless authentication.
                const auto& it = m_backend_pwds.find(res.mapped_user);
                if (it != m_backend_pwds.end())
                {
                    MXB_INFO("Using password found in backend passwords file for '%s'.",
                             res.mapped_user.c_str());
                    auto begin = it->second.pw_hash;
                    auto end = begin + SHA_DIGEST_LENGTH;
                    session->backend_token.assign(begin, end);
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

PamClientAuthenticator::PamClientAuthenticator(bool cleartext_plugin, AuthMode mode,
                                               BackendMapping be_mapping, const PasswordMap& backend_pwds)
    : m_cleartext_plugin(cleartext_plugin)
    , m_mode(mode)
    , m_be_mapping(be_mapping)
    , m_backend_pwds(backend_pwds)
{
}

Buffer PamClientAuthenticator::create_2fa_prompt_packet() const
{
    /**
     * 4 bytes     - Header
     * byte        - Message type
     * string[EOF] - Message
     */
    size_t plen = 1 + TWO_FA_QUERY.length();
    size_t buflen = MYSQL_HEADER_LEN + plen;
    uint8_t bufdata[buflen];
    uint8_t* pData = bufdata;
    mariadb::set_byte3(pData, plen);
    pData += 3;
    *pData++ = m_sequence;
    *pData++ = DIALOG_ECHO_DISABLED;    // Equivalent to server 2FA prompt
    memcpy(pData, TWO_FA_QUERY.c_str(), TWO_FA_QUERY.length());
    Buffer buffer(bufdata, buflen);
    return buffer;
}
