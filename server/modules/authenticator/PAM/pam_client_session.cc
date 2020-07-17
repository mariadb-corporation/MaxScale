/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_client_session.hh"

#include <set>
#include <maxbase/pam_utils.hh>
#include <maxscale/event.hh>
#include "pam_instance.hh"
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

using maxscale::Buffer;
using std::string;
using AuthRes = mariadb::ClientAuthenticator::AuthRes;
using mariadb::UserEntry;

namespace
{
/**
 * @brief Read the client's password, store it to MySQL-session
 *
 * @param buffer Buffer containing the password
 * @return True on success, false if memory allocation failed
 */
bool store_client_password(MYSQL_session* session, GWBUF* buffer)
{
    bool rval = false;
    uint8_t header[MYSQL_HEADER_LEN];

    if (gwbuf_copy_data(buffer, 0, MYSQL_HEADER_LEN, header) == MYSQL_HEADER_LEN)
    {
        size_t plen = gw_mysql_get_byte3(header);
        session->auth_token.resize(plen);
        gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, plen, session->auth_token.data());
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
    size_t plen = dialog ? (1 + DIALOG_SIZE + 1 + PASSWORD.length()) : (1 + CLEAR_PW_SIZE);
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
        memcpy(pData, PASSWORD.c_str(), PASSWORD.length());     // First message
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
        // Client should have responses with password.
        if (store_client_password(session, buffer))
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

    /** We sent the authentication change packet + plugin name and the client
     * responded with the password. Try to continue authentication without more
     * messages to client. */

    // take username from the session object, not the user entry. The entry may be anonymous.
    string username = session->user;
    string password((char*)session->auth_token.data(), session->auth_token.size());

    // The server PAM plugin uses "mysql" as the default service when authenticating
    // a user with no service.
    string pam_service = entry->auth_string.empty() ? "mysql" : entry->auth_string;
    AuthResult res = mxb::pam::authenticate(username, password, session->remote, pam_service, PASSWORD);
    if (res.type == AuthResult::Result::SUCCESS)
    {
        rval.status = AuthRes::Status::SUCCESS;
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

PamClientAuthenticator::PamClientAuthenticator(bool cleartext_plugin)
    : m_cleartext_plugin(cleartext_plugin)
{
}
