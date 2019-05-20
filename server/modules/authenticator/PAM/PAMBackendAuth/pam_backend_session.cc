/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_backend_session.hh"
#include <maxscale/server.h>

namespace
{
/**
 * Check that the AuthSwitchRequest packet is as expected. Partially an inverse of
 * create_auth_change_packet() in pam_auth.cc.
 *
 * To deal with arbitrary messages in the packet, this needs to mirror also the server
 * plugin code.
 *
 * @param dcb Backend DCB
 * @param buffer Buffer containing an AuthSwitchRequest packet
 * @return True on success, false on error
 */
bool check_auth_switch_request(DCB* dcb, GWBUF* buffer)
{
    /**
     * The server PAM plugin sends data usually once, at the moment it gets a prompt-type message
     * from the api. The "message"-segment may contain multiple messages from the api separated by \n.
     * MaxScale should ignore this text and search for "Password: " near the end of the message. If
     * server sends more data, authentication ends in error, but this should only happen if the server
     * asks multiple questions.
     *
     * The AuthSwitchRequest packet:
     * 4 bytes     - Header
     * 0xfe        - Command byte
     * string[NUL] - Auth plugin name, should be "dialog"
     * byte        - Message type, 2 or 4
     * string[EOF] - Message(s)
     *
     * Authenticators receive complete packets from protocol.
     */

    // Smallest buffer that is parsed, header + cmd byte.
    int min_readable_buflen = MYSQL_HEADER_LEN + 1;
    // The buffer should be small, don't accept big messages since it most likely means a complicated auth
    // scheme.
    const int MAX_BUFLEN = 2000;

    // Smallest buffer with every packet component, although the components may be wrong.
    int min_msg_buflen = min_readable_buflen + 3;
    // Smallest buffer with all expected data.
    int min_acceptable_buflen = MYSQL_HEADER_LEN + 1 + DIALOG_SIZE + 1 + PASSWORD.length();

    bool rval = false;
    const char* srv_name = dcb->server->name;
    int buflen = gwbuf_length(buffer);
    if (buflen <= min_readable_buflen || buflen > MAX_BUFLEN)
    {
        MXB_ERROR("Authentication start packet from '%s' is %i bytes. Expected length of packet is "
                  "between %i and %i.", srv_name, buflen, min_acceptable_buflen, MAX_BUFLEN);
        // Lengths between min_readable_buflen and min_acceptable_buflen are checked below.
    }
    else
    {
        uint8_t data[buflen + 1]; // + 1 to ensure that the end has a zero.
        data[buflen] = 0;
        gwbuf_copy_data(buffer, 0, buflen, data);
        uint8_t cmdbyte = data[MYSQL_HEADER_LEN];
        if (cmdbyte == MYSQL_REPLY_AUTHSWITCHREQUEST)
        {
            bool malformed_packet = false;
            // Correct packet type.
            if (buflen >= min_msg_buflen)
            {
                // Buffer is long enough to contain at least some of the fields. Try to read plugin name.
                const char* ptr = reinterpret_cast<char*>(data) + min_readable_buflen;
                const char* end = reinterpret_cast<char*>(data) + buflen;
                if (strcmp(ptr, DIALOG.c_str()) == 0)
                {
                    // Correct plugin.
                    ptr += DIALOG_SIZE;
                    if (end - ptr >= 2) // message type + message
                    {
                        int msg_type = *ptr++;
                        if (msg_type == DIALOG_ECHO_ENABLED || msg_type == DIALOG_ECHO_DISABLED)
                        {
                            // The rest of the buffer contains a message.
                            // The server separates messages with linebreaks. Search for the last.
                            const char* linebrk_pos = strrchr(ptr, '\n');
                            if (linebrk_pos)
                            {
                                int msg_len = linebrk_pos - ptr;
                                MXS_INFO("Server '%s' PAM plugin sent messages: '%.*s'",
                                         srv_name, msg_len, ptr);
                                ptr = linebrk_pos + 1;
                            }

                            if (ptr == PASSWORD)
                            {
                                rval = true;
                            }
                            else
                            {
                                MXB_ERROR("'%s' asked for '%s' when '%s was expected.",
                                          srv_name, ptr, PASSWORD.c_str());
                            }
                        }
                        else
                        {
                            malformed_packet = true;
                        }
                    }
                    else
                    {
                        malformed_packet = true;
                    }
                }
                else
                {
                    MXB_ERROR("'%s' asked for authentication plugin '%s' when '%s' was expected.",
                              srv_name, ptr, DIALOG.c_str());
                }
            }
            else
            {
                malformed_packet = true;
            }

            if (malformed_packet)
            {
                MXB_ERROR("Received malformed AuthSwitchRequest-packet from '%s'.", srv_name);
            }
        }
        else if (cmdbyte == MYSQL_REPLY_OK)
        {
            // Authentication is already done? Maybe the server authenticated us as the anonymous user. This
            // is quite insecure. */
            MXB_ERROR("Authentication to '%s' was complete before it even started, "
                      "anonymous users may be enabled.", srv_name);
        }
        else
        {
            MXB_ERROR("Expected AuthSwitchRequest-packet from '%s' but received %#x.", srv_name, cmdbyte);
        }
    }
    return rval;
}
}
PamBackendSession::PamBackendSession()
    : m_state(PAM_AUTH_INIT)
    , m_sequence(0)
{
}

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
    gwbuf_copy_data(buffer, MYSQL_SEQ_OFFSET, 1, &m_sequence);
    m_sequence++;
    bool rval = false;

    if (m_state == PAM_AUTH_INIT && check_auth_switch_request(dcb, buffer))
    {
        rval = true;
    }
    else if (m_state == PAM_AUTH_DATA_SENT)
    {
        /** Read authentication response */
        if (mxs_mysql_is_ok_packet(buffer))
        {
            MXS_DEBUG("pam_backend_auth_extract received ok packet from '%s'.", dcb->server->name);
            m_state = PAM_AUTH_OK;
            rval = true;
        }
        else
        {
            MXS_ERROR("Expected ok from server but got something else. Authentication failed.");
        }
    }

    if (!rval)
    {
        MXS_DEBUG("pam_backend_auth_extract to backend '%s' failed for user '%s'.",
                  dcb->server->name,
                  dcb->user);
    }
    return rval;
}

int PamBackendSession::authenticate(DCB* dcb)
{
    int rval = MXS_AUTH_FAILED;

    if (m_state == PAM_AUTH_INIT)
    {
        MXS_DEBUG("pam_backend_auth_authenticate sending password to '%s'.",
                  dcb->server->name);
        if (send_client_password(dcb))
        {
            rval = MXS_AUTH_INCOMPLETE;
            m_state = PAM_AUTH_DATA_SENT;
        }
    }
    else if (m_state == PAM_AUTH_OK)
    {
        rval = MXS_AUTH_SUCCEEDED;
    }

    return rval;
}
