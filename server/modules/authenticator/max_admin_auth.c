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

#include <gw_authenticator.h>
#include <maxscale/alloc.h>
#include <modinfo.h>
#include <dcb.h>
#include <buffer.h>
#include <adminusers.h>
#include <users.h>

/* @see function load_module in load_utils.c for explanation of the following
 * lint directives.
 */
/*lint -e14 */
MODULE_INFO info =
{
    MODULE_API_AUTHENTICATOR,
    MODULE_GA,
    GWAUTHENTICATOR_VERSION,
    "The MaxScale Admin client authenticator implementation"
};
/*lint +e14 */

static char *version_str = "V2.1.0";

static int max_admin_auth_set_protocol_data(DCB *dcb, GWBUF *buf);
static bool max_admin_auth_is_client_ssl_capable(DCB *dcb);
static int max_admin_auth_authenticate(DCB *dcb);
static void max_admin_auth_free_client_data(DCB *dcb);

/*
 * The "module object" for mysql client authenticator module.
 */
static GWAUTHENTICATOR MyObject =
{
    NULL,                                 /* No create entry point */
    max_admin_auth_set_protocol_data,     /* Extract data into structure   */
    max_admin_auth_is_client_ssl_capable, /* Check if client supports SSL  */
    max_admin_auth_authenticate,          /* Authenticate user credentials */
    max_admin_auth_free_client_data,      /* Free the client data held in DCB */
    NULL,                                 /* No destroy entry point */
    users_default_loadusers               /* Load generic users */
};

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
/* @see function load_module in load_utils.c for explanation of the following
 * lint directives.
*/
/*lint -e14 */
char* version()
{
    return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void ModuleInit()
{
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
GWAUTHENTICATOR* GetModuleObject()
{
    return &MyObject;
}
/*lint +e14 */

/**
 * @brief Authentication of a user/password combination.
 *
 * The validation is already done, the result is returned.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Authentication status - always 0 to denote success
 */
static int
max_admin_auth_authenticate(DCB *dcb)
{
    return (dcb->data != NULL && ((ADMIN_session *)dcb->data)->validated) ? 0 : 1;
}

/**
 * @brief Transfer data from the authentication request to the DCB.
 *
 * Expects a chain of two buffers as the second parameters, with the
 * username in the first buffer and the password in the second buffer.
 *
 * @param dcb Request handler DCB connected to the client
 * @param buffer Pointer to pointer to buffers containing data from client
 * @return Authentication status - 0 for success, 1 for failure
 */
static int
max_admin_auth_set_protocol_data(DCB *dcb, GWBUF *buf)
{
    ADMIN_session *session_data;

    max_admin_auth_free_client_data(dcb);

    if ((session_data = (ADMIN_session *)MXS_CALLOC(1, sizeof(ADMIN_session))) != NULL)
    {
        int user_len = (GWBUF_LENGTH(buf) > ADMIN_USER_MAXLEN) ? ADMIN_USER_MAXLEN : GWBUF_LENGTH(buf);
#if defined(SS_DEBUG)
        session_data->adminses_chk_top = CHK_NUM_ADMINSES;
        session_data->adminses_chk_tail = CHK_NUM_ADMINSES;
#endif
        memcpy(session_data->user, GWBUF_DATA(buf), user_len);
        session_data->validated = false;
        dcb->data = (void *)session_data;

        /* Check for existance of the user */
        if (admin_linux_account_enabled(session_data->user))
        {
            session_data->validated = true;
            return 0;
        }
    }
    return 1;
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
static bool
max_admin_auth_is_client_ssl_capable(DCB *dcb)
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
static void
max_admin_auth_free_client_data(DCB *dcb)
{
    MXS_FREE(dcb->data);
}
