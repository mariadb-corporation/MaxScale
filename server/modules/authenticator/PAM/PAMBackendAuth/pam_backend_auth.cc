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

#include "pam_backend_auth.hh"

#include <maxscale/authenticator.h>
#include <maxscale/log_manager.h>
#include <maxscale/server.h>
#include "pam_backend_session.hh"
#include "../pam_auth_common.hh"

static void* pam_backend_auth_alloc(void *instance)
{
    PamBackendSession* pses = new (std::nothrow) PamBackendSession();
    return pses;
}

static void pam_backend_auth_free(void *data)
{
    delete static_cast<PamBackendSession*>(data);
}

/**
 * @brief Extract data from a MySQL packet
 *
 * @param dcb Backend DCB
 * @param buffer Buffer containing a complete packet
 *
 * @return MXS_AUTH_INCOMPLETE if authentication is ongoing, MXS_AUTH_SUCCEEDED
 * if authentication is complete and MXS_AUTH_FAILED if authentication failed.
 */
static bool pam_backend_auth_extract(DCB *dcb, GWBUF *buffer)
{
    PamBackendSession *pses = static_cast<PamBackendSession*>(dcb->authenticator_data);
    return pses->extract(dcb, buffer);
}

/**
 * @brief Check whether the DCB supports SSL
 *
 * @param dcb Backend DCB
 *
 * @return True if DCB supports SSL
 */
static bool pam_backend_auth_connectssl(DCB *dcb)
{
    return dcb->server->server_ssl != NULL;
}

/**
 * @brief Authenticate to backend. Should be called after extract()
 *
 * @param dcb Backend DCB
 *
 * @return MXS_AUTH_INCOMPLETE if authentication is ongoing, MXS_AUTH_SUCCEEDED
 * if authentication is complete and MXS_AUTH_FAILED if authentication failed.
 */
static int pam_backend_auth_authenticate(DCB *dcb)
{
    PamBackendSession *pses = static_cast<PamBackendSession*>(dcb->authenticator_data);
    return pses->authenticate(dcb);
}

extern "C"
{
/**
 * Module handle entry point
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_AUTHENTICATOR MyObject =
    {
        NULL,                            /* No initialize entry point */
        pam_backend_auth_alloc,          /* Allocate authenticator data */
        pam_backend_auth_extract,        /* Extract data into structure   */
        pam_backend_auth_connectssl,     /* Check if client supports SSL  */
        pam_backend_auth_authenticate,   /* Authenticate user credentials */
        NULL,                            /* Client plugin will free shared data */
        pam_backend_auth_free,           /* Free authenticator data */
        NULL,                            /* Load users from backend databases */
        NULL,                            /* No diagnostic */
        NULL,
        NULL                             /* No user reauthentication */
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

}
