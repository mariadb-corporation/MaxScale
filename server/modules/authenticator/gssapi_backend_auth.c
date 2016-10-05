/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <gw_authenticator.h>
#include <maxscale/alloc.h>
#include <dcb.h>
#include <mysql_client_server_protocol.h>
#include "gssapi_auth.h"

/**
 * @file gssapi_backend_auth.c GSSAPI backend authenticator
 */

/** TODO: Document functions and GSSAPI specific code */

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

    target.value = auth->principal_name;
    target.length = auth->principal_name_len + 1;

    major = gss_import_name(&minor, &target, GSS_C_NT_USER_NAME, &princ);

    if (GSS_ERROR(major))
    {
        report_error(major, minor);
    }

    major = gss_init_sec_context(&minor, GSS_C_NO_CREDENTIAL,
                                 &handle, princ, GSS_C_NO_OID, 0, 0,
                                 GSS_C_NO_CHANNEL_BINDINGS, &in, NULL, &out, 0, 0);
    if (GSS_ERROR(major))
    {
        report_error(major, minor);
    }
    else
    {
        GWBUF *buffer = gwbuf_alloc(MYSQL_HEADER_LEN + out.length);

        if (buffer)
        {
            uint8_t *data = (uint8_t*)GWBUF_DATA(buffer);
            gw_mysql_set_byte3(data, out.length);
            data += 3;
            *data++ = 0x03;
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

bool extract_principal_name(DCB *dcb, GWBUF *buffer)
{
    bool rval = false;
    size_t buflen = gwbuf_length(buffer) - MYSQL_HEADER_LEN;
    uint8_t databuf[buflen];
    gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, buflen, databuf);
    uint8_t *data = databuf;

    while (*data)
    {
        data++;
    }

    data++;
    buflen -= data - databuf;

    uint8_t *principal = MXS_MALLOC(buflen);

    if (principal)
    {
        memcpy(principal, data, buflen);
        gssapi_auth_t *auth = (gssapi_auth_t*)dcb->authenticator_data;
        auth->principal_name = principal;
        auth->principal_name_len = buflen;
        rval = true;
    }

    return rval;
}

static int gssapi_backend_auth_extract(DCB *dcb, GWBUF *buffer)
{
    int rval = MXS_AUTH_FAILED;
    gssapi_auth_t *data = (gssapi_auth_t*)dcb->authenticator_data;

    if (data->state == GSSAPI_AUTH_INIT && extract_principal_name(dcb, buffer))
    {
        rval = MXS_AUTH_INCOMPLETE;
    }
    else if (data->state == GSSAPI_AUTH_DATA_SENT)
    {
        /** Read authentication response */
        if (mxs_mysql_is_ok_packet(buffer))
        {
            data->state = GSSAPI_AUTH_OK;
            rval = MXS_AUTH_SUCCEEDED;
        }
    }

    return rval;
}

static bool gssapi_backend_auth_connectssl(DCB *dcb)
{
    return dcb->server->server_ssl != NULL;
}

static int gssapi_backend_auth_authenticate(DCB *dcb)
{
    int rval = MXS_AUTH_FAILED;
    gssapi_auth_t *auth_data = (gssapi_auth_t*)dcb->authenticator_data;

    if (auth_data->state == GSSAPI_AUTH_INIT)
    {
        if (send_new_auth_token(dcb))
        {
            rval = MXS_AUTH_INCOMPLETE;
            auth_data->state = GSSAPI_AUTH_DATA_SENT;
        }

    }
    else if (auth_data->state == GSSAPI_AUTH_OK)
    {
        rval = MXS_AUTH_SUCCEEDED;
    }

    return rval;
}

/**
 * Implementation of the authenticator module interface
 */
static GWAUTHENTICATOR MyObject =
{
    gssapi_auth_alloc,                  /* Allocate authenticator data */
    gssapi_backend_auth_extract,        /* Extract data into structure   */
    gssapi_backend_auth_connectssl,     /* Check if client supports SSL  */
    gssapi_backend_auth_authenticate,   /* Authenticate user credentials */
    NULL,                               /* Client plugin will free shared data */
    gssapi_auth_free,                   /* Free authenticator data */
    NULL                                /* Load users from backend databases */
};

MODULE_INFO info =
{
    MODULE_API_AUTHENTICATOR,
    MODULE_GA,
    GWAUTHENTICATOR_VERSION,
    "GSSAPI backend authenticator"
};

static char *version_str = "V1.0.0";

/**
 * Version string entry point
 */
char* version()
{
    return version_str;
}

/**
 * Module initialization entry point
 */
void ModuleInit()
{
}

/**
 * Module handle entry point
 */
GWAUTHENTICATOR* GetModuleObject()
{
    return &MyObject;
}
