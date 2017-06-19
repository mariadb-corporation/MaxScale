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

#define MXS_MODULE_NAME "PAMBackendAuth"

#include "../pam_auth.hh"

#include <maxscale/authenticator.h>
#include <maxscale/alloc.h>
#include <maxscale/buffer.hh>
#include <maxscale/dcb.h>
#include <maxscale/log_manager.h>
#include <maxscale/protocol/mysql.h>


/**
 * @file pam_backend_auth.c - PAM backend authenticator
 */

namespace
{
/**
 * Send password to server
 *
 * @param dcb Backend DCB
 * @return True on success, false on error
 */
bool send_client_password(DCB *dcb)
{
    bool rval = false;
    MYSQL_session *ses = (MYSQL_session*)dcb->session->client_dcb->data;
    PamSession* pses = static_cast<PamSession*>(dcb->authenticator_data);
    size_t buflen = MYSQL_HEADER_LEN + ses->auth_token_len;
    uint8_t bufferdata[buflen];
    gw_mysql_set_byte3(bufferdata, ses->auth_token_len);
    bufferdata[MYSQL_SEQ_OFFSET] = pses->m_sequence++;
    memcpy(bufferdata + MYSQL_HEADER_LEN, ses->auth_token, ses->auth_token_len);
    maxscale::Buffer pwbuf(bufferdata, buflen);
    return dcb_write(dcb, pwbuf.release());
}

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
    const char ERRMSG_P2[] = "Only simple password-based PAM authentication is supported.";
    /**
     * The AuthSwitchRequest packet:
     * 4 bytes     - Header
     * 0xfe        - Command byte
     * string[NUL] - Auth plugin name
     * byte        - Message type
     * string[EOF] - Message
     */
    /** We know how long the packet should be in the simple case. */
    unsigned int expected_buflen = MYSQL_HEADER_LEN + 1 + sizeof(DIALOG) +
                                   1 + sizeof(PASSWORD) - 1 /* no terminating 0 */;
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
                  buflen, expected_buflen, ERRMSG_P2);
        return false;
    }

    PamSession *pses = static_cast<PamSession*>(dcb->authenticator_data);
    pses->m_sequence = data[MYSQL_SEQ_OFFSET] + 1;

    /* Check that the server is using the "dialog" plugin and asking for the password. */
    uint8_t* plugin_name_loc = data + MYSQL_HEADER_LEN + 1;
    uint8_t* msg_type_loc = plugin_name_loc + sizeof(DIALOG);
    uint8_t msg_type = *msg_type_loc;
    uint8_t* msg_loc = msg_type_loc + 1;

    bool rval = false;
    if ((strcmp((char*)plugin_name_loc, DIALOG) == 0) &&
        // 2 and 4 are constants used by the dialog plugin
        (msg_type == 2 || msg_type == 4) &&
        strncmp((char*)msg_loc, PASSWORD, sizeof(PASSWORD) - 1) == 0)
    {
        rval = true;
    }
    else
    {
        MXS_ERROR("AuthSwitchRequest packet contents unexpected. %s", ERRMSG_P2);
    }
    return rval;
}

}

void* pam_backend_auth_alloc(void *instance)
{
    PamSession* pses = new PamSession;
    return pses;
}

void pam_backend_auth_free(void *data)
{
    delete static_cast<PamSession*>(data);
}

/**
 * @brief Extract data from a MySQL packet
 * @param dcb Backend DCB
 * @param buffer Buffer containing a complete packet
 * @return MXS_AUTH_INCOMPLETE if authentication is ongoing, MXS_AUTH_SUCCEEDED
 * if authentication is complete and MXS_AUTH_FAILED if authentication failed.
 */
static int pam_backend_auth_extract(DCB *dcb, GWBUF *buffer)
{
    int rval = MXS_AUTH_FAILED;
    PamSession *pses = static_cast<PamSession*>(dcb->authenticator_data);

    if (pses->m_state == PAM_AUTH_INIT && check_auth_switch_request(dcb, buffer))
    {
        rval = MXS_AUTH_INCOMPLETE;
    }
    else if (pses->m_state == PAM_AUTH_DATA_SENT)
    {
        /** Read authentication response */
        if (mxs_mysql_is_ok_packet(buffer))
        {
            MXS_DEBUG("pam_backend_auth_extract received ok packet from '%s'.",
                      dcb->server->unique_name);
            pses->m_state = PAM_AUTH_OK;
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

/**
 * @brief Check whether the DCB supports SSL
 * @param dcb Backend DCB
 * @return True if DCB supports SSL
 */
static bool pam_backend_auth_connectssl(DCB *dcb)
{
    return dcb->server->server_ssl != NULL;
}

/**
 * @brief Authenticate the backend connection
 * @param dcb Backend DCB
 * @return MXS_AUTH_INCOMPLETE if authentication is ongoing, MXS_AUTH_SUCCEEDED
 * if authentication is complete and MXS_AUTH_FAILED if authentication failed.
 */
static int pam_backend_auth_authenticate(DCB *dcb)
{
    int rval = MXS_AUTH_FAILED;
    PamSession *pses = static_cast<PamSession*>(dcb->authenticator_data);

    if (pses->m_state == PAM_AUTH_INIT)
    {
        MXS_DEBUG("pam_backend_auth_authenticate sending password to '%s'.",
                  dcb->server->unique_name);
        if (send_client_password(dcb))
        {
            rval = MXS_AUTH_INCOMPLETE;
            pses->m_state = PAM_AUTH_DATA_SENT;
        }
    }
    else if (pses->m_state == PAM_AUTH_OK)
    {
        rval = MXS_AUTH_SUCCEEDED;
    }

    return rval;
}

MXS_BEGIN_DECLS
/**
 * Module handle entry point
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_AUTHENTICATOR MyObject =
    {
        NULL,                               /* No initialize entry point */
        pam_backend_auth_alloc,          /* Allocate authenticator data */
        pam_backend_auth_extract,        /* Extract data into structure   */
        pam_backend_auth_connectssl,     /* Check if client supports SSL  */
        pam_backend_auth_authenticate,   /* Authenticate user credentials */
        NULL,                               /* Client plugin will free shared data */
        pam_backend_auth_free,           /* Free authenticator data */
        NULL,                               /* Load users from backend databases */
        NULL,                               /* No diagnostic */
        NULL,
        NULL                                /* No user reauthentication */
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_AUTHENTICATOR,
        MXS_MODULE_ALPHA_RELEASE,
        MXS_AUTHENTICATOR_VERSION,
        "PAM backend authenticator",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        { { MXS_END_MODULE_PARAMS} }
    };

    return &info;
}
MXS_END_DECLS