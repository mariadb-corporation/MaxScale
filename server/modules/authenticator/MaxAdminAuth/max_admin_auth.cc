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

/**
 * @file max_admin_auth.c
 *
 * MaxScale Admin Authentication module for checking of clients credentials
 * for access to MaxAdmin.  Might be usable for other purposes.
 *
 * @verbatim
 * Revision History
 * Date         Who                     Description
 * 14/03/2016   Martin Brampton         Initial version
 * 17/05/2016   Massimiliano Pinto      New version authenticates UNIX user
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "MaxAdminAuth"

#include <maxscale/authenticator.h>
#include <maxscale/alloc.h>
#include <maxscale/modinfo.h>
#include <maxscale/dcb.h>
#include <maxscale/buffer.h>
#include <maxscale/adminusers.h>
#include <maxscale/users.h>

static bool max_admin_auth_set_protocol_data(DCB* dcb, GWBUF* buf);
static bool max_admin_auth_is_client_ssl_capable(DCB* dcb);
static int  max_admin_auth_authenticate(DCB* dcb);
static void max_admin_auth_free_client_data(DCB* dcb);

extern "C"
{
/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
    MXS_MODULE* MXS_CREATE_MODULE()
    {
        static MXS_AUTHENTICATOR MyObject =
        {
            NULL,                                   /* No initialize entry point */
            NULL,                                   /* No create entry point */
            max_admin_auth_set_protocol_data,       /* Extract data into structure   */
            max_admin_auth_is_client_ssl_capable,   /* Check if client supports SSL  */
            max_admin_auth_authenticate,            /* Authenticate user credentials */
            max_admin_auth_free_client_data,        /* Free the client data held in DCB */
            NULL,                                   /* No destroy entry point */
            users_default_loadusers,                /* Load generic users */
            users_default_diagnostic,               /* Default user diagnostic */
            users_default_diagnostic_json,          /* Default user diagnostic */
            NULL                                    /* No user reauthentication */
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_AUTHENTICATOR,
            MXS_MODULE_GA,
            MXS_AUTHENTICATOR_VERSION,
            "The MaxScale Admin client authenticator implementation",
            "V2.1.0",
            MXS_NO_MODULE_CAPABILITIES,
            &MyObject,
            NULL,   /* Process init. */
            NULL,   /* Process finish. */
            NULL,   /* Thread init. */
            NULL,   /* Thread finish. */
            {{MXS_END_MODULE_PARAMS}}
        };

        return &info;
    }
/*lint +e14 */
}

/**
 * @brief Authentication of a user/password combination.
 *
 * The validation is already done, the result is returned.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Authentication status - always 0 to denote success
 */
static int max_admin_auth_authenticate(DCB* dcb)
{
    return (dcb->data != NULL && ((ADMIN_session*)dcb->data)->validated) ? 0 : 1;
}

/**
 * @brief Transfer data from the authentication request to the DCB.
 *
 * Expects a chain of two buffers as the second parameters, with the
 * username in the first buffer and the password in the second buffer.
 *
 * @param dcb Request handler DCB connected to the client
 * @param buffer Pointer to pointer to buffers containing data from client
 * @return Authentication status - true for success, false for failure
 */
static bool max_admin_auth_set_protocol_data(DCB* dcb, GWBUF* buf)
{
    ADMIN_session* session_data;

    max_admin_auth_free_client_data(dcb);

    if ((session_data = (ADMIN_session*)MXS_CALLOC(1, sizeof(ADMIN_session))) != NULL)
    {
        int user_len = (GWBUF_LENGTH(buf) > ADMIN_USER_MAXLEN) ? ADMIN_USER_MAXLEN : GWBUF_LENGTH(buf);
        memcpy(session_data->user, GWBUF_DATA(buf), user_len);
        session_data->validated = false;
        dcb->data = (void*)session_data;

        /* Check for existance of the user */
        if (admin_linux_account_enabled(session_data->user))
        {
            session_data->validated = true;
            return true;
        }
    }
    return false;
}

/**
 * @brief Determine whether the client is SSL capable
 *
 * Always say that client is not SSL capable. Support for SSL is not yet
 * available.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Boolean indicating whether client is SSL capable - false
 */
static bool max_admin_auth_is_client_ssl_capable(DCB* dcb)
{
    return false;
}

/**
 * @brief Free the client data pointed to by the passed DCB.
 *
 * The max_admin authenticator uses a simple structure that can be freed with
 * a single call to MXS_FREE().
 *
 * @param dcb Request handler DCB connected to the client
 */
static void max_admin_auth_free_client_data(DCB* dcb)
{
    MXS_FREE(dcb->data);
}
