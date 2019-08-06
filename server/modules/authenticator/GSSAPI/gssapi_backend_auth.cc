/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "gssapi_auth.hh"

#include <maxbase/alloc.h>
#include <maxscale/dcb.hh>
#include <maxscale/protocol/mysql.hh>
#include <maxscale/server.hh>

/**
 * @file gssapi_backend_auth.c - GSSAPI backend authenticator
 */

GSSAPIBackendAuthenticatorSession* GSSAPIBackendAuthenticatorSession::newSession()
{
    auto rval = new (std::nothrow) GSSAPIBackendAuthenticatorSession();

    if (rval)
    {
        rval->state = GSSAPI_AUTH_INIT;
        rval->principal_name = NULL;
        rval->principal_name_len = 0;
        rval->sequence = 0;
    }

    return rval;
}

GSSAPIBackendAuthenticatorSession::~GSSAPIBackendAuthenticatorSession()
{
    MXS_FREE(principal_name);
}

/**
 * @brief Create a new GSSAPI token
 * @param dcb Backend DCB
 * @return True on success, false on error
 */
bool GSSAPIBackendAuthenticatorSession::send_new_auth_token(DCB* dcb)
{
    bool rval = false;
    auto auth = this;
    MYSQL_session* ses = (MYSQL_session*)dcb->session()->client_dcb->m_data;
    GWBUF* buffer = gwbuf_alloc(MYSQL_HEADER_LEN + ses->auth_token_len);

    // This function actually just forwards the client's token to the backend server

    if (buffer)
    {
        uint8_t* data = (uint8_t*)GWBUF_DATA(buffer);
        gw_mysql_set_byte3(data, ses->auth_token_len);
        data += 3;
        *data++ = ++auth->sequence;
        memcpy(data, ses->auth_token, ses->auth_token_len);

        if (dcb_write(dcb, buffer))
        {
            rval = true;
        }
    }

    return rval;
}

/**
 * @brief Extract the principal name from the AuthSwitchRequest packet
 *
 * @param dcb Backend DCB
 * @param buffer Buffer containing an AuthSwitchRequest packet
 * @return True on success, false on error
 */
bool GSSAPIBackendAuthenticatorSession::extract_principal_name(DCB* dcb, GWBUF* buffer)
{
    bool rval = false;
    size_t buflen = gwbuf_length(buffer) - MYSQL_HEADER_LEN;
    uint8_t databuf[buflen];
    uint8_t* data = databuf;
    auto auth = this;

    /** Copy the payload and the current packet sequence number */
    gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, buflen, databuf);
    gwbuf_copy_data(buffer, MYSQL_SEQ_OFFSET, 1, &auth->sequence);

    if (databuf[0] != MYSQL_REPLY_AUTHSWITCHREQUEST)
    {
        /** Server responded with something we did not expect. If it's an OK packet,
         * it's possible that the server authenticated us as the anonymous user. This
         * means that the server is not secure. */
        MXS_ERROR("Server '%s' returned an unexpected authentication response.%s",
                  dcb->m_server->name(),
                  databuf[0] == MYSQL_REPLY_OK ?
                  " Authentication was complete before it even started, "
                  "anonymous users might not be disabled." : "");
        return false;
    }

    /**
     * The AuthSwitchRequest packet
     *
     * 0xfe        - Command byte
     * string[NUL] - Auth plugin name
     * string[EOF] - Auth plugin data
     *
     * Skip over the auth plugin name and copy the service principal name stored
     * in the auth plugin data section.
     */
    while (*data && data < databuf + buflen)
    {
        data++;
    }

    data++;
    buflen -= data - databuf;

    if (buflen > 0)
    {
        uint8_t* principal = static_cast<uint8_t*>(MXS_MALLOC(buflen + 1));

        if (principal)
        {
            /** Store the principal name for later when we request the token
             * from the GSSAPI server */
            memcpy(principal, data, buflen);
            principal[buflen] = '\0';
            auth->principal_name = principal;
            auth->principal_name_len = buflen;
            rval = true;
        }
    }
    else
    {
        MXS_ERROR("Backend server did not send any auth plugin data.");
    }

    return rval;
}

/**
 * @brief Extract data from a MySQL packet
 * @param dcb Backend DCB
 * @param buffer Buffer containing a complete packet
 * @return True if authentication is ongoing or complete,
 * false if authentication failed.
 */
bool GSSAPIBackendAuthenticatorSession::extract(DCB* dcb, GWBUF* buffer)
{
    bool rval = false;
    auto auth = this;

    if (auth->state == GSSAPI_AUTH_INIT && extract_principal_name(dcb, buffer))
    {
        rval = true;
    }
    else if (auth->state == GSSAPI_AUTH_DATA_SENT)
    {
        /** Read authentication response */
        if (mxs_mysql_is_ok_packet(buffer))
        {
            auth->state = GSSAPI_AUTH_OK;
            rval = true;
        }
    }

    return rval;
}

/**
 * @brief Check whether the DCB supports SSL
 * @param dcb Backend DCB
 * @return True if DCB supports SSL
 */
bool GSSAPIBackendAuthenticatorSession::ssl_capable(DCB* dcb)
{
    return dcb->m_server->ssl().context() != NULL;
}

/**
 * @brief Authenticate the backend connection
 * @param dcb Backend DCB
 * @return MXS_AUTH_INCOMPLETE if authentication is ongoing, MXS_AUTH_SUCCEEDED
 * if authentication is complete and MXS_AUTH_FAILED if authentication failed.
 */
int GSSAPIBackendAuthenticatorSession::authenticate(DCB* dcb)
{
    int rval = MXS_AUTH_FAILED;
    auto auth = this;

    if (auth->state == GSSAPI_AUTH_INIT)
    {
        if (send_new_auth_token(dcb))
        {
            rval = MXS_AUTH_INCOMPLETE;
            auth->state = GSSAPI_AUTH_DATA_SENT;
        }
    }
    else if (auth->state == GSSAPI_AUTH_OK)
    {
        rval = MXS_AUTH_SUCCEEDED;
    }

    return rval;
}
