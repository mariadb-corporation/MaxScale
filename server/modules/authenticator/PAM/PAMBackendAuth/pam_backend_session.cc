/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_backend_session.hh"
#include <maxscale/server.hh>

/**
 * Parse packet type and plugin name from packet data. Advances pointer.
 *
 * @param data Data from server. The pointer is advanced.
 * @param end Pointer to after the end of data
 * @return True if all expected fields were parsed
 */
bool PamBackendSession::parse_authswitchreq(const uint8_t** data, const uint8_t* end)
{
    const uint8_t* ptr = *data;
    if (ptr >= end)
    {
        return false;
    }

    const char* server_name = m_servername.c_str();
    bool success = false;
    uint8_t cmdbyte = *ptr++;
    if (cmdbyte == MYSQL_REPLY_AUTHSWITCHREQUEST)
    {
        // Correct packet type.
        if (ptr < end)
        {
            const char* plugin_name = reinterpret_cast<const char*>(ptr);
            if (strcmp(plugin_name, DIALOG.c_str()) == 0)
            {
                // Correct plugin.
                ptr += DIALOG_SIZE;
                success = true;
            }
            else
            {
                MXB_ERROR("'%s' asked for authentication plugin '%s' when authenticating '%s'. "
                          "Only '%s' is supported.",
                          server_name, plugin_name, m_clienthost.c_str(), DIALOG.c_str());
            }
        }
        else
        {
            MXB_ERROR("Received malformed AuthSwitchRequest-packet from '%s'.", server_name);
        }
    }
    else if (cmdbyte == MYSQL_REPLY_OK)
    {
        // Authentication is already done? Maybe the server authenticated us as the anonymous user. This
        // is quite insecure. */
        MXB_ERROR("Authentication of '%s' to '%s' was complete before it even started, anonymous users may "
                  "be enabled.", m_clienthost.c_str(), server_name);
    }
    else
    {
        MXB_ERROR("Expected AuthSwitchRequest-packet from '%s' but received %#x.", server_name, cmdbyte);
    }

    if (success)
    {
        *data = ptr;
    }
    return success;
}

/**
 * Parse prompt type and message text from packet data. Advances pointer.
 *
 * @param data Data from server. The pointer is advanced.
 * @param end Pointer to after the end of data
 * @return True if all expected fields were parsed
 */
bool PamBackendSession::parse_password_prompt(const uint8_t** data, const uint8_t* end)
{
    const uint8_t* ptr = *data;
    if (end - ptr < 2) // Need at least message type + message
    {
        return false;
    }

    const char* server_name = m_servername.c_str();
    bool success = false;
    int msg_type = *ptr++;
    if (msg_type == DIALOG_ECHO_ENABLED || msg_type == DIALOG_ECHO_DISABLED)
    {
        const char* messages = reinterpret_cast<const char*>(ptr);
        // The rest of the buffer contains a message.
        // The server separates messages with linebreaks. Search for the last.
        const char* linebrk_pos = strrchr(messages, '\n');
        const char* prompt;
        if (linebrk_pos)
        {
            int msg_len = linebrk_pos - messages;
            MXS_INFO("'%s' sent message when authenticating '%s': '%.*s'",
                     server_name, m_clienthost.c_str(), msg_len, messages);
            prompt = linebrk_pos + 1;
        }
        else
        {
            prompt = messages; // No additional messages.
        }

        if (prompt == PASSWORD)
        {
            success = true;
        }
        else
        {
            MXB_ERROR("'%s' asked for '%s' when authenticating '%s'. '%s' was expected.",
                      server_name, prompt, m_clienthost.c_str(), PASSWORD.c_str());
        }
    }
    else
    {
        MXB_ERROR("'%s' sent an unknown message type %i when authenticating '%s'.",
                  server_name, msg_type, m_clienthost.c_str());
    }

    if (success)
    {
        *data = ptr;
    }
    return success;
}

PamBackendSession::PamBackendSession()
{}

/**
 * Send password to server
 *
 * @param dcb Backend DCB
 * @return True on success, false on error
 */
bool PamBackendSession::send_client_password(DCB* dcb)
{
    MYSQL_session* ses = (MYSQL_session*)dcb->session->client_dcb->data;
    size_t buflen = MYSQL_HEADER_LEN + ses->auth_token_len;
    uint8_t bufferdata[buflen];
    gw_mysql_set_byte3(bufferdata, ses->auth_token_len);
    bufferdata[MYSQL_SEQ_OFFSET] = m_sequence;
    memcpy(bufferdata + MYSQL_HEADER_LEN, ses->auth_token, ses->auth_token_len);
    return dcb_write(dcb, gwbuf_alloc_and_load(buflen, bufferdata));
}

bool PamBackendSession::extract(DCB* dcb, GWBUF* buffer)
{
    /**
     * The server PAM plugin sends data usually once, at the moment it gets a prompt-type message
     * from the api. The "message"-segment may contain multiple messages from the api separated by \n.
     * MaxScale should ignore this text and search for "Password: " near the end of the message. See
     * https://github.com/MariaDB/server/blob/10.3/plugin/auth_pam/auth_pam.c
     * for how communication is handled on the other side.
     *
     * The AuthSwitchRequest packet:
     * 4 bytes     - Header
     * 0xfe        - Command byte
     * string[NUL] - Auth plugin name, should be "dialog"
     * byte        - Message type, 2 or 4
     * string[EOF] - Message(s)
     *
     * Additional prompts after AuthSwitchRequest:
     * 4 bytes     - Header
     * byte        - Message type, 2 or 4
     * string[EOF] - Message(s)
     *
     * Authenticators receive complete packets from protocol.
     */

    const char* srv_name = dcb->server->name();
    if (m_servername.empty())
    {
        m_servername = srv_name;
        auto client_dcb = dcb->session->client_dcb;
        m_clienthost = client_dcb->user + (std::string)"@" + client_dcb->remote;
    }

    // Smallest buffer that is parsed, header + (cmd-byte/msg-type + message).
    const int min_readable_buflen = MYSQL_HEADER_LEN + 1 + 1;
    // The buffer should be reasonable size. Large buffers likely mean that the auth scheme is complicated.
    const int MAX_BUFLEN = 2000;
    const int buflen = gwbuf_length(buffer);
    if (buflen <= min_readable_buflen || buflen > MAX_BUFLEN)
    {
        MXB_ERROR("Received packet of size %i from '%s' during authentication. Expected packet size is "
                  "between %i and %i.", buflen, srv_name, min_readable_buflen, MAX_BUFLEN);
        return false;
    }

    uint8_t data[buflen + 1]; // + 1 to ensure that the end has a zero.
    data[buflen] = 0;
    gwbuf_copy_data(buffer, 0, buflen, data);
    m_sequence = data[MYSQL_SEQ_OFFSET] + 1;
    const uint8_t* data_ptr = data + MYSQL_COM_OFFSET;
    const uint8_t* end_ptr = data + buflen;
    bool success = false;
    bool unexpected_data = false;

    switch (m_state)
    {
        case State::INIT:
        // Server should have sent the AuthSwitchRequest. If server version is 10.4, the server may not
        // send a prompt. Older versions add the first prompt to the same packet.
        if (parse_authswitchreq(&data_ptr, end_ptr))
        {
            if (end_ptr > data_ptr)
            {
                if (parse_password_prompt(&data_ptr, end_ptr))
                {
                    m_state = State::RECEIVED_PROMPT;
                    success = true;
                }
                else
                {
                    // Password prompt should have been there, but was not.
                    unexpected_data = true;
                }
            }
            else
            {
                // Just the AuthSwitchRequest, this is ok. The server now expects a password so set state
                // accordingly.
                m_state = State::RECEIVED_PROMPT;
                success = true;
            }
        }
        else
        {
            // No AuthSwitchRequest, error.
            unexpected_data = true;
        }
        break;

        case State::PW_SENT:
        {
            /** Read authentication response. This is typically either OK packet or ERROR, but can be another
             *  prompt. */
            uint8_t cmdbyte = data[MYSQL_COM_OFFSET];
            if (cmdbyte == MYSQL_REPLY_OK)
            {
                MXS_DEBUG("pam_backend_auth_extract received ok packet from '%s'.", srv_name);
                m_state = State::DONE;
                success = true;
            }
            else if (cmdbyte == MYSQL_REPLY_ERR)
            {
                MXS_DEBUG("pam_backend_auth_extract received error packet from '%s'.", srv_name);
                m_state = State::DONE;
            }
            else
            {
                // The packet may contain another prompt, try parse it. Currently, it's expected to be
                // another "Password: ", in the future other setups may be supported.
                if (parse_password_prompt(&data_ptr, end_ptr))
                {
                    m_state = State::RECEIVED_PROMPT;
                    success = true;
                }
                else
                {
                    MXS_ERROR("Expected OK, ERR or PAM prompt from '%s' but received something else. ",
                              srv_name);
                    unexpected_data = true;
                }
            }
        }
        break;


        default:
            // This implicates an error in either PAM authenticator or backend protocol.
            mxb_assert(!true);
            unexpected_data = true;
            break;
    }

    if (unexpected_data)
    {
        MXS_ERROR("Failed to read data from '%s' when authenticating user '%s'.", srv_name, dcb->user);
    }
    return success;
}

int PamBackendSession::authenticate(DCB* dcb)
{
    int rval = MXS_AUTH_FAILED;

    if (m_state == State::RECEIVED_PROMPT)
    {
        MXS_DEBUG("pam_backend_auth_authenticate sending password to '%s'.", dcb->server->name());
        if (send_client_password(dcb))
        {
            m_state = State::PW_SENT;
            rval = MXS_AUTH_INCOMPLETE;
        }
        else
        {
            m_state = State::DONE;
        }
    }
    else if (m_state == State::DONE)
    {
        rval = MXS_AUTH_SUCCEEDED;
    }

    return rval;
}
