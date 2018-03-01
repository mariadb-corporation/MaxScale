/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file null_auth_allow.c
 *
 * Null Authentication module for handling the checking of clients credentials
 * for protocols that do not have authentication, either temporarily or
 * permanently
 *
 * @verbatim
 * Revision History
 * Date         Who                     Description
 * 14/03/2016   Martin Brampton         Initial version
 * 17/05/2016   Martin Brampton         Renamed
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "NullAuthAllow"

#include <maxscale/authenticator.h>
#include <maxscale/modinfo.h>
#include <maxscale/dcb.h>
#include <maxscale/buffer.h>
#include <maxscale/users.h>

/** MXS-1026: Without MySQL protocol data structures, the NullAuth authenticator will crash. */
#include <maxscale/protocol/mysql.h>

static int null_auth_set_protocol_data(DCB *dcb, GWBUF *buf);
static bool null_auth_is_client_ssl_capable(DCB *dcb);
static int null_auth_authenticate(DCB *dcb);
static void null_auth_free_client_data(DCB *dcb);

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
        NULL,                            /* No initialize entry point */
        NULL,                            /* No create entry point */
        null_auth_set_protocol_data,     /* Extract data into structure   */
        null_auth_is_client_ssl_capable, /* Check if client supports SSL  */
        null_auth_authenticate,          /* Authenticate user credentials */
        null_auth_free_client_data,      /* Free the client data held in DCB */
        NULL,                            /* No destroy entry point */
        users_default_loadusers,         /* Load generic users */
        NULL,                            /* No diagnostic */
        NULL                             /* No user reauthentication */
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_AUTHENTICATOR,
        MXS_MODULE_GA,
        MXS_AUTHENTICATOR_VERSION,
        "The Null client authenticator implementation",
        "V1.1.0",
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
/*lint +e14 */

/**
 * @brief Null authentication of a user.
 *
 * Always returns success
 *
 * @param dcb Request handler DCB connected to the client
 * @return Authentication status - always 0 to denote success
 */
static int
null_auth_authenticate(DCB *dcb)
{
    return 0;
}

/**
 * @brief Transfer data from the authentication request to the DCB.
 *
 * Does not actually transfer any data
 *
 * @param dcb Request handler DCB connected to the client
 * @param buffer Pointer to pointer to buffer containing data from client
 * @return Authentication status - always 0 to indicate success
 */
static int
null_auth_set_protocol_data(DCB *dcb, GWBUF *buf)
{
    /** MXS-1026: This will just prevent a crash when the NullAuth authenticator
     * is used. This does not provide a way to use MaxScale with no authentication. */
    dcb->data = calloc(1, sizeof(MYSQL_session));
    dcb->protocol = mysql_protocol_init(dcb, dcb->fd);

    return 0;
}

/**
 * @brief Determine whether the client is SSL capable
 *
 * Always say that client is SSL capable.  The null authenticator cannot be
 * used in a context where the client is not SSL capable.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Boolean indicating whether client is SSL capable - always true
 */
static bool
null_auth_is_client_ssl_capable(DCB *dcb)
{
    return true;
}

/**
 * @brief Free the client data pointed to by the passed DCB.
 *
 * The null authenticator does not allocate any data, so nothing to do.
 *
 * @param dcb Request handler DCB connected to the client
 */
static void
null_auth_free_client_data(DCB *dcb)
{
    free(dcb->data);
    dcb->data = NULL;
}
