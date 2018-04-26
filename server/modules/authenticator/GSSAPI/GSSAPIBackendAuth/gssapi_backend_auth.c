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

#define MXS_MODULE_NAME "GSSAPIBackendAuth"

#include <maxscale/alloc.h>
#include <maxscale/authenticator.h>
#include <maxscale/dcb.h>
#include <maxscale/log_manager.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/server.h>

#include "../gssapi_auth.h"

/**
 * @file gssapi_backend_auth.c - GSSAPI backend authenticator
 */

void* gssapi_backend_auth_alloc(void *instance)
{
    gssapi_auth_t* rval = MXS_MALLOC(sizeof(gssapi_auth_t));

    if (rval)
    {
        rval->state = GSSAPI_AUTH_INIT;
        rval->principal_name = NULL;
        rval->principal_name_len = 0;
        rval->sequence = 0;
    }

    return rval;
}

void gssapi_backend_auth_free(void *data)
{
    if (data)
    {
        gssapi_auth_t *auth = (gssapi_auth_t*)data;
        MXS_FREE(auth->principal_name);
        MXS_FREE(auth);
    }
}

/**
 * @brief Create a new GSSAPI token
 * @param dcb Backend DCB
 * @return True on success, false on error
 */
static bool send_new_auth_token(DCB *dcb)
{
    bool rval = false;
    OM_uint32 major = 0, minor = 0;
    gss_ctx_id_t handle = NULL;
    gss_buffer_desc in = {0, 0};
    gss_buffer_desc out = {0, 0};
    gss_buffer_desc target = {0, 0};
    gss_name_t princ = GSS_C_NO_NAME;
    gssapi_auth_t *auth = (gssapi_auth_t*)dcb->authenticator_data;

    /** The service principal name is sent by the backend server */
    target.value = auth->principal_name;
    target.length = auth->principal_name_len + 1;

    /** Convert the name into GSSAPI format */
    major = gss_import_name(&minor, &target, GSS_C_NT_USER_NAME, &princ);

    if (GSS_ERROR(major))
    {
        report_error(major, minor);
    }

    /** Request the token for the service */
    major = gss_init_sec_context(&minor, GSS_C_NO_CREDENTIAL,
                                 &handle, princ, GSS_C_NO_OID, 0, 0,
                                 GSS_C_NO_CHANNEL_BINDINGS, &in, NULL, &out, 0, 0);
    if (GSS_ERROR(major))
    {
        report_error(major, minor);
    }
    else
    {
        /** We successfully requested the token, send it to the backend server */
        GWBUF *buffer = gwbuf_alloc(MYSQL_HEADER_LEN + out.length);

        if (buffer)
        {
            uint8_t *data = (uint8_t*)GWBUF_DATA(buffer);
            gw_mysql_set_byte3(data, out.length);
            data += 3;
            *data++ = ++auth->sequence;
            memcpy(data, out.value, out.length);

            if (dcb_write(dcb, buffer))
            {
                rval = true;
            }
        }

        major = gss_delete_sec_context(&minor, &handle, &in);

        if (GSS_ERROR(major))
        {
            report_error(major, minor);
        }

        major = gss_release_name(&minor, &princ);

        if (GSS_ERROR(major))
        {
            report_error(major, minor);
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
bool extract_principal_name(DCB *dcb, GWBUF *buffer)
{
    bool rval = false;
    size_t buflen = gwbuf_length(buffer) - MYSQL_HEADER_LEN;
    uint8_t databuf[buflen];
    uint8_t *data = databuf;
    gssapi_auth_t *auth = (gssapi_auth_t*)dcb->authenticator_data;

    /** Copy the payload and the current packet sequence number */
    gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, buflen, databuf);
    gwbuf_copy_data(buffer, MYSQL_SEQ_OFFSET, 1, &auth->sequence);

    if (databuf[0] != MYSQL_REPLY_AUTHSWITCHREQUEST)
    {
        /** Server responded with something we did not expect. If it's an OK packet,
         * it's possible that the server authenticated us as the anonymous user. This
         * means that the server is not secure. */
        MXS_ERROR("Server '%s' returned an unexpected authentication response.%s",
                  dcb->server->name, databuf[0] == MYSQL_REPLY_OK ?
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
        uint8_t *principal = MXS_MALLOC(buflen + 1);

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
static bool gssapi_backend_auth_extract(DCB *dcb, GWBUF *buffer)
{
    bool rval = false;
    gssapi_auth_t *auth = (gssapi_auth_t*)dcb->authenticator_data;

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
static bool gssapi_backend_auth_connectssl(DCB *dcb)
{
    return dcb->server->server_ssl != NULL;
}

/**
 * @brief Authenticate the backend connection
 * @param dcb Backend DCB
 * @return MXS_AUTH_INCOMPLETE if authentication is ongoing, MXS_AUTH_SUCCEEDED
 * if authentication is complete and MXS_AUTH_FAILED if authentication failed.
 */
static int gssapi_backend_auth_authenticate(DCB *dcb)
{
    int rval = MXS_AUTH_FAILED;
    gssapi_auth_t *auth = (gssapi_auth_t*)dcb->authenticator_data;

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

/**
 * Module handle entry point
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_AUTHENTICATOR MyObject =
    {
        NULL,                               /* No initialize entry point */
        gssapi_backend_auth_alloc,          /* Allocate authenticator data */
        gssapi_backend_auth_extract,        /* Extract data into structure   */
        gssapi_backend_auth_connectssl,     /* Check if client supports SSL  */
        gssapi_backend_auth_authenticate,   /* Authenticate user credentials */
        NULL,                               /* Client plugin will free shared data */
        gssapi_backend_auth_free,           /* Free authenticator data */
        NULL,                               /* Load users from backend databases */
        NULL,                               /* No diagnostic */
        NULL,
        NULL                                /* No user reauthentication */
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_AUTHENTICATOR,
        MXS_MODULE_GA,
        MXS_AUTHENTICATOR_VERSION,
        "GSSAPI backend authenticator",
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
