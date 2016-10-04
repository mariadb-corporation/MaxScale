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
 * @file null_auth_deny.c
 *
 * Null Authentication module for handling the checking of clients credentials
 * for protocols that do not have authentication, either temporarily or
 * permanently. Always fails the authentication.
 *
 * @verbatim
 * Revision History
 * Date         Who                     Description
 * 14/03/2016   Martin Brampton         Initial version
 * 17/05/2016   Martin Brampton         Version to fail, instead of succeed.
 *
 * @endverbatim
 */

#include <gw_authenticator.h>
#include <modinfo.h>
#include <dcb.h>
#include <buffer.h>
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
    "The Null client authenticator implementation"
};
/*lint +e14 */

static char *version_str = "V1.1.0";

static int null_auth_set_protocol_data(DCB *dcb, GWBUF *buf);
static bool null_auth_is_client_ssl_capable(DCB *dcb);
static int null_auth_authenticate(DCB *dcb);
static void null_auth_free_client_data(DCB *dcb);

/*
 * The "module object" for mysql client authenticator module.
 */
static GWAUTHENTICATOR MyObject =
{
    NULL,                            /* No create entry point */
    null_auth_set_protocol_data,     /* Extract data into structure   */
    null_auth_is_client_ssl_capable, /* Check if client supports SSL  */
    null_auth_authenticate,          /* Authenticate user credentials */
    null_auth_free_client_data,      /* Free the client data held in DCB */
    NULL,                            /* No destroy entry point */
    users_default_loadusers          /* Load generic users */
};

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 *
 * @see function load_module in load_utils.c for explanation of the following
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
 * @brief Null authentication of a user.
 *
 * Always returns success
 *
 * @param dcb Request handler DCB connected to the client
 * @return Authentication status - always 1 to denote failure
 */
static int
null_auth_authenticate(DCB *dcb)
{
    return 1;
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
null_auth_free_client_data(DCB *dcb) {}
