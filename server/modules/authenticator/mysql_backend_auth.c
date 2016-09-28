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
 * @file mysql_backend_auth.c - MySQL backend authenticator
 *
 * Backend authentication module for the MySQL protocol. Implements the
 * client side of the 'mysql_native_password' authentication plugin.
 *
 * @verbatim
 * Revision History
 * Date         Who                     Description
 * 27/09/2016   Markus Makela           Initial version
 *
 * @endverbatim
 */

#include <gw_authenticator.h>
#include <mysql_client_server_protocol.h>
#include <maxscale/alloc.h>
#include <utils.h>

/** Authentication states */
enum mba_state
{
    MBA_NEED_HANDSHAKE,         /**< Waiting for server's handshake packet */
    MBA_SEND_RESPONSE,          /**< A response to the server's handshake has been sent */
    MBA_NEED_OK,                /**< Waiting for server's OK packet */
    MBA_AUTH_OK,                /**< Authentication completed successfully */
    MBA_AUTH_FAILED             /**< Authentication failed */
};

/** Structure representing the authentication state */
typedef struct mysql_backend_auth
{
    enum mba_state state; /**< Authentication state */
} mysql_backend_auth_t;

/**
 * @brief Allocate a new mysql_backend_auth object
 * @return Allocated object or NULL if memory allocation failed
 */
mysql_backend_auth_t* mba_alloc()
{
    mysql_backend_auth_t* mba = MXS_MALLOC(sizeof(*mba));

    if (mba)
    {
        mba->state = MBA_NEED_HANDSHAKE;
    }

    return mba;
}

/**
 * Receive the MySQL authentication packet from backend, packet # is 2
 *
 * @param protocol The MySQL protocol structure
 * @return False in case of failure, true if authentication was successful.
 */
static bool gw_read_auth_response(DCB *dcb, GWBUF *buffer)
{
    bool rval = false;
    uint8_t cmd;

    if (gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, 1, &cmd) &&  cmd == MYSQL_REPLY_OK)
    {
        rval = true;
    }

    return rval;
}

/**
 * @brief Extract backend response
 *
 * @param dcb Request handler DCB connected to the client
 * @param buffer Buffer containing data from client
 * @return Authentication status
 * @see gw_quthenticator.h
 * @see https://dev.mysql.com/doc/internals/en/client-server-protocol.html
 */
static int
auth_backend_extract(DCB *dcb, GWBUF *buf)
{
    int rval = MXS_AUTH_FAILED;

    if (dcb->backend_data || (dcb->backend_data = mba_alloc()))
    {
        mysql_backend_auth_t *mba = (mysql_backend_auth_t*)dcb->backend_data;

        switch (mba->state)
        {
            case MBA_NEED_HANDSHAKE:
                if (gw_read_backend_handshake(dcb, buf))
                {
                    rval = MXS_AUTH_INCOMPLETE;
                    mba->state = MBA_SEND_RESPONSE;
                }
                else
                {
                    mba->state = MBA_AUTH_FAILED;
                }
                break;

            case MBA_NEED_OK:
                if (gw_read_auth_response(dcb, buf))
                {
                    rval = MXS_AUTH_SUCCEEDED;
                    mba->state =  MBA_AUTH_OK;
                }
                else
                {
                    mba->state = MBA_AUTH_FAILED;
                }
                break;

            default:
                MXS_ERROR("Unexpected call to MySQLBackendAuth::extract");
                ss_dassert(false);
                break;
        }
    }

    return rval;
}

/**
 * @brief Authenticates as a MySQL user
 *
 * @param dcb Backend DCB
 * @return Authentication status
 * @see gw_authenticator.h
 */
static int
auth_backend_authenticate(DCB *dcb)
{
    int rval = MXS_AUTH_FAILED;
    mysql_backend_auth_t *mba = (mysql_backend_auth_t*)dcb->backend_data;

    if (mba->state == MBA_SEND_RESPONSE)
    {
        /** First message read, decode password and send the auth credentials to backend */
        switch (gw_send_backend_auth(dcb))
        {
            case MXS_AUTH_STATE_CONNECTED:
                rval = MXS_AUTH_SSL_INCOMPLETE;
                break;

            case MXS_AUTH_STATE_RESPONSE_SENT:
                mba->state = MBA_NEED_OK;
                rval = MXS_AUTH_INCOMPLETE;
                break;

            default:
                /** Authentication failed */
                break;
        }
    }
    else if (mba->state == MBA_AUTH_OK)
    {
        /** Authentication completed successfully */
        rval = MXS_AUTH_SUCCEEDED;
    }

    return rval;
}

/**
 * @brief Determine whether the client is SSL capable
 *
 * The authentication request from the client will indicate whether the client
 * is expecting to make an SSL connection. The information has been extracted
 * in the previous functions.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Boolean indicating whether client is SSL capable
 */
static bool
auth_backend_ssl(DCB *dcb)
{
    return dcb->server->server_ssl != NULL;
}

/**
 * @brief Dummy function for the free entry point
 */
static void
auth_backend_free(DCB *dcb)
{
    MXS_FREE(dcb->backend_data);
    dcb->backend_data = NULL;
}

/**
 * @brief Dummy function for the loadusers entry point
 */
static int auth_backend_load_users(SERV_LISTENER *port)
{
    return MXS_AUTH_LOADUSERS_OK;
}

/* @see function load_module in load_utils.c for explanation of the following
 * lint directives.
*/
/*lint -e14 */
MODULE_INFO info =
{
    MODULE_API_AUTHENTICATOR,
    MODULE_GA,
    GWAUTHENTICATOR_VERSION,
    "The MySQL MaxScale to backend server authenticator"
};
/*lint +e14 */

static char *version_str = "V1.0.0";

/*
 * The "module object" for mysql client authenticator module.
 */
static GWAUTHENTICATOR MyObject =
{
    auth_backend_extract,       /* Extract data into structure   */
    auth_backend_ssl,           /* Check if client supports SSL  */
    auth_backend_authenticate,  /* Authenticate user credentials */
    auth_backend_free,          /* Free the client data held in DCB */
    auth_backend_load_users,    /* Load users from backend databases */
    DEFAULT_MYSQL_AUTH_PLUGIN
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
