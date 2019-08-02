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

#define MXS_MODULE_NAME "HTTPAuth"

#include <maxscale/authenticator.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/users.h>

static int http_auth_authenticate(DCB* dcb)
{
    return 0;
}

static bool http_auth_set_protocol_data(DCB* dcb, GWBUF* buf)
{
    return true;
}

static bool http_auth_is_client_ssl_capable(DCB* dcb)
{
    return false;
}

static void http_auth_free_client_data(DCB* dcb)
{
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_AUTHENTICATOR MyObject =
    {
        NULL,                               /* No initialize entry point */
        NULL,                               /* No create entry point */
        http_auth_set_protocol_data,        /* Extract data into structure   */
        http_auth_is_client_ssl_capable,    /* Check if client supports SSL  */
        http_auth_authenticate,             /* Authenticate user credentials */
        http_auth_free_client_data,         /* Free the client data held in DCB */
        NULL,                               /* No destroy entry point */
        users_default_loadusers,            /* Load generic users */
        users_default_diagnostic,           /* Default user diagnostic */
        users_default_diagnostic_json,      /* Default user diagnostic */
        NULL                                /* No user reauthentication */
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_AUTHENTICATOR,
        MXS_MODULE_GA,
        MXS_AUTHENTICATOR_VERSION,
        "The MaxScale HTTP authenticator (does nothing)",
        "V2.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &MyObject,
        NULL,       /* Process init. */
        NULL,       /* Process finish. */
        NULL,       /* Thread init. */
        NULL,       /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
