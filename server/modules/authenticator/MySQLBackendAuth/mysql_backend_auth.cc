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
 * @file mysql_backend_auth.c - MySQL backend authenticator
 *
 * Backend authentication module for the MySQL protocol. Implements the
 * client side of the 'mysql_native_password' authentication plugin.
 *
 * The "heavy lifting" of the authentication is done by the protocol module so
 * the only thing left for this module is to read the final OK packet from the
 * server.
 */

#define MXS_MODULE_NAME "MySQLBackendAuth"

#include <maxscale/alloc.h>
#include <maxscale/authenticator.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/server.hh>
#include <maxscale/utils.h>

/** Authentication states */
enum mba_state
{
    MBA_NEED_OK,                /**< Waiting for server's OK packet */
    MBA_AUTH_OK,                /**< Authentication completed successfully */
    MBA_AUTH_FAILED             /**< Authentication failed */
};

/** Structure representing the authentication state */
typedef struct mysql_backend_auth
{
    enum mba_state state;   /**< Authentication state */
} mysql_backend_auth_t;

/**
 * @brief Allocate a new mysql_backend_auth object
 * @return Allocated object or NULL if memory allocation failed
 */
void* auth_backend_create(void* instance)
{
    mysql_backend_auth_t* mba = static_cast<mysql_backend_auth_t*>(MXS_MALLOC(sizeof(*mba)));

    if (mba)
    {
        mba->state = MBA_NEED_OK;
    }

    return mba;
}

/**
 * @brief Free allocated mysql_backend_auth object
 * @param data Allocated mysql_backend_auth object
 */
void auth_backend_destroy(void* data)
{
    if (data)
    {
        MXS_FREE(data);
    }
}
/**
 * @brief Extract backend response
 *
 * @param dcb Request handler DCB connected to the client
 * @param buffer Buffer containing data from client
 * @return True on success, false on error
 * @see authenticator.h
 */
static bool auth_backend_extract(DCB* dcb, GWBUF* buf)
{
    bool rval = false;
    mysql_backend_auth_t* mba = (mysql_backend_auth_t*)dcb->authenticator_data;

    switch (mba->state)
    {
    case MBA_NEED_OK:
        if (mxs_mysql_is_ok_packet(buf))
        {
            rval = true;
            mba->state = MBA_AUTH_OK;
        }
        else
        {
            mba->state = MBA_AUTH_FAILED;
        }
        break;

    default:
        MXS_ERROR("Unexpected call to MySQLBackendAuth::extract");
        mxb_assert(false);
        break;
    }

    return rval;
}

/**
 * @brief Authenticates as a MySQL user
 *
 * @param dcb Backend DCB
 * @return Authentication status
 * @see authenticator.h
 */
static int auth_backend_authenticate(DCB* dcb)
{
    int rval = MXS_AUTH_FAILED;
    mysql_backend_auth_t* mba = (mysql_backend_auth_t*)dcb->authenticator_data;

    if (mba->state == MBA_AUTH_OK)
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
static bool auth_backend_ssl(DCB* dcb)
{
    return dcb->server->server_ssl != NULL;
}

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
            NULL,                       /* No initialize entry point */
            auth_backend_create,        /* Create authenticator */
            auth_backend_extract,       /* Extract data into structure   */
            auth_backend_ssl,           /* Check if client supports SSL  */
            auth_backend_authenticate,  /* Authenticate user credentials */
            NULL,                       /* The shared data is freed by the client DCB */
            auth_backend_destroy,       /* Destroy authenticator */
            NULL,                       /* We don't need to load users */
            NULL,                       /* No diagnostic */
            NULL,
            NULL                    /* No user reauthentication */
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_AUTHENTICATOR,
            MXS_MODULE_GA,
            MXS_AUTHENTICATOR_VERSION,
            "The MySQL MaxScale to backend server authenticator",
            "V1.0.0",
            MXS_NO_MODULE_CAPABILITIES,
            &MyObject,
            NULL,   /* Process init. */
            NULL,   /* Process finish. */
            NULL,   /* Thread init. */
            NULL,   /* Thread finish. */
            {
                {MXS_END_MODULE_PARAMS}
            }
        };

        return &info;
    }
/*lint +e14 */
}
