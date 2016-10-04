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
#include <gssapi.h>
#include "gssapi_auth.h"

/**
 * @file gssapi_backend_auth.c GSSAPI backend authenticator
 */

static int gssapi_backend_auth_extract(DCB *dcb, GWBUF *buffer)
{
    gw_send_backend_auth(dcb);
    return MXS_AUTH_SUCCEEDED;
}

static bool gssapi_backend_auth_connectssl(DCB *dcb)
{
    return dcb->server->server_ssl != NULL;
}

static int gssapi_backend_auth_authenticate(DCB *dcb)
{
    return MXS_AUTH_SUCCEEDED;
}

/**
 * Implementation of the authenticator module interface
 */
static GWAUTHENTICATOR MyObject =
{
    gssapi_auth_alloc,
    gssapi_backend_auth_extract,             /* Extract data into structure   */
    gssapi_backend_auth_connectssl,          /* Check if client supports SSL  */
    gssapi_backend_auth_authenticate,        /* Authenticate user credentials */
    NULL,
    gssapi_auth_free,
    NULL                       /* Load users from backend databases */
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
