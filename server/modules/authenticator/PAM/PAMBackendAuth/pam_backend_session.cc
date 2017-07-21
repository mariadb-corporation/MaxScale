/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_backend_session.hh"

namespace
{
    /**
 * Check that the AuthSwitchRequest packet is as expected. Inverse of
 * create_auth_change_packet() in pam_auth.cc.
 *
 * @param dcb Backend DCB
 * @param buffer Buffer containing an AuthSwitchRequest packet
 * @return True on success, false on error
 */
bool check_auth_switch_request(DCB *dcb, GWBUF *buffer)
{
    /**
     * The AuthSwitchRequest packet:
     * 4 bytes     - Header
     * 0xfe        - Command byte
     * string[NUL] - Auth plugin name
     * byte        - Message type
     * string[EOF] - Message
     */
    /** We know how long the packet should be in the simple case. */
    unsigned int expected_buflen = MYSQL_HEADER_LEN + 1 + DIALOG_SIZE + 1 + PASSWORD.length();
    uint8_t data[expected_buflen];
    size_t copied = gwbuf_copy_data(buffer, 0, expected_buflen, data);

    /* Check that this is an AuthSwitchRequest. */
    if ((copied <= MYSQL_HEADER_LEN) || (data[MYSQL_HEADER_LEN] != MYSQL_REPLY_AUTHSWITCHREQUEST))
    {
        /** Server responded with something we did not expect. If it's an OK packet,
         * it's possible that the server authenticated us as the anonymous user. This
         * means that the server is not secure. */
        bool was_ok_packet = copied > MYSQL_HEADER_LEN &&
                             data[MYSQL_HEADER_LEN + 1] == MYSQL_REPLY_OK;
        MXS_ERROR("Server '%s' returned an unexpected authentication response.%s",
                  dcb->server->unique_name, was_ok_packet ?
                  " Authentication was complete before it even started, "
                  "anonymous users might not be disabled." : "");
        return false;
    }
    unsigned int buflen = gwbuf_length(buffer);
    if (buflen != expected_buflen)
    {
        MXS_ERROR("Length of server AuthSwitchRequest packet was '%u', expected '%u'. %s",
                  buflen, expected_buflen, GENERAL_ERRMSG);
        return false;
    }

    /* Check that the server is using the "dialog" plugin and asking for the password. */
    uint8_t* plugin_name_loc = data + MYSQL_HEADER_LEN + 1;
    uint8_t* msg_type_loc = plugin_name_loc + DIALOG_SIZE;
    uint8_t msg_type = *msg_type_loc;
    uint8_t* msg_loc = msg_type_loc + 1;

    bool rval = false;
    if ((DIALOG == (char*)plugin_name_loc) &&
        (msg_type == DIALOG_ECHO_ENABLED || msg_type == DIALOG_ECHO_DISABLED) &&
        PASSWORD.compare(0, PASSWORD.length(), (char*)msg_loc, PASSWORD.length()) == 0)
    {
        rval = true;
    }
    else
    {
        MXS_ERROR("AuthSwitchRequest packet contents unexpected. %s", GENERAL_ERRMSG);
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
bool PamBackendSession::send_client_password(DCB *dcb)
{
    bool rval = false;
    MYSQL_session *ses = (MYSQL_session*)dcb->session->client_dcb->data;
    size_t buflen = MYSQL_HEADER_LEN + ses->auth_token_len;
    uint8_t bufferdata[buflen];
    gw_mysql_set_byte3(bufferdata, ses->auth_token_len);
    bufferdata[MYSQL_SEQ_OFFSET] = m_sequence;
    memcpy(bufferdata + MYSQL_HEADER_LEN, ses->auth_token, ses->auth_token_len);
    return dcb_write(dcb, gwbuf_alloc_and_load(buflen, bufferdata));
}

int PamBackendSession::extract(DCB *dcb, GWBUF *buffer)
{
    gwbuf_copy_data(buffer, MYSQL_SEQ_OFFSET, 1, &m_sequence);
    m_sequence++;
    int rval = MXS_AUTH_FAILED;

    if (m_state == PAM_AUTH_INIT && check_auth_switch_request(dcb, buffer))
    {
        rval = MXS_AUTH_INCOMPLETE;
    }
    else if (m_state == PAM_AUTH_DATA_SENT)
    {
        /** Read authentication response */
        if (mxs_mysql_is_ok_packet(buffer))
        {
            MXS_DEBUG("pam_backend_auth_extract received ok packet from '%s'.",
                      dcb->server->unique_name);
            m_state = PAM_AUTH_OK;
            rval = MXS_AUTH_SUCCEEDED;
        }
        else
        {
            MXS_ERROR("Expected ok from server but got something else. Authentication"
                      " failed.");
        }
    }

    if (rval == MXS_AUTH_FAILED)
    {
        MXS_DEBUG("pam_backend_auth_extract to backend '%s' failed for user '%s'.",
                  dcb->server->unique_name, dcb->user);
    }
    return rval;
}

int PamBackendSession::authenticate(DCB *dcb)
{
    int rval = MXS_AUTH_FAILED;

    if (m_state == PAM_AUTH_INIT)
    {
        MXS_DEBUG("pam_backend_auth_authenticate sending password to '%s'.",
                  dcb->server->unique_name);
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
