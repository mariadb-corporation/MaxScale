#ifndef GW_AUTHENTICATOR_H
#define GW_AUTHENTICATOR_H
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
 * @file protocol.h
 *
 * The authenticator module interface definitions for MaxScale
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 17/02/16     Martin Brampton         Initial implementation
 *
 * @endverbatim
 */

#include <buffer.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/dh.h>

struct dcb;
struct server;
struct session;
struct servlistener;

/**
 * @verbatim
 * The operations that can be performed on the descriptor
 *
 *      extract         Extract the data from a buffer and place in a structure
 *      connectssl      Determine whether the connection can support SSL
 *      authenticate    Carry out the authentication
 *      free            Free extracted data
 *      loadusers       Load or update authenticator user data
 *      plugin_name     The protocol specific name of the authentication plugin.
 * @endverbatim
 *
 * This forms the "module object" for authenticator modules within the gateway.
 *
 * @see load_module
 */
typedef struct gw_authenticator
{
    int (*extract)(struct dcb *, GWBUF *);
    bool (*connectssl)(struct dcb *);
    int (*authenticate)(struct dcb *);
    void (*free)(struct dcb *);
    int (*loadusers)(struct servlistener *);
    const char* plugin_name;
} GWAUTHENTICATOR;

/** Return values for extract and authenticate entry points */
#define MXS_AUTH_SUCCEEDED 0 /**< Authentication was successful */
#define MXS_AUTH_FAILED 1 /**< Authentication failed */
#define MXS_AUTH_FAILED_DB 2 /**< Authentication failed, database not found */
#define MXS_AUTH_FAILED_SSL 3 /**< SSL authentication failed */
#define MXS_AUTH_INCOMPLETE 4 /**< Authentication is not yet complete */
#define MXS_AUTH_SSL_INCOMPLETE 5 /**< SSL connection is not yet complete */
#define MXS_AUTH_NO_SESSION 6

/** Return values for the loadusers entry point */
#define MXS_AUTH_LOADUSERS_OK    0 /**< Users loaded successfully */
#define MXS_AUTH_LOADUSERS_ERROR 1 /**< Failed to load users */

/**
 * Authentication states
 *
 * The state usually goes from INIT to CONNECTED and alternates between
 * MESSAGE_READ and RESPONSE_SENT until ending up in either FAILED or COMPLETE.
 *
 * If the server immediately rejects the connection, the state ends up in
 * HANDSHAKE_FAILED. If the connection creation would block, instead of going to
 * the CONNECTED state, the connection will be in PENDING_CONNECT state until
 * the connection can be created.
 */
typedef enum
{
    MXS_AUTH_STATE_INIT, /**< Initial authentication state */
    MXS_AUTH_STATE_PENDING_CONNECT,/**< Connection creation is underway */
    MXS_AUTH_STATE_CONNECTED, /**< Network connection to server created */
    MXS_AUTH_STATE_MESSAGE_READ, /**< Read a authentication message from the server */
    MXS_AUTH_STATE_RESPONSE_SENT, /**< Responded to the read authentication message */
    MXS_AUTH_STATE_FAILED, /**< Authentication failed */
    MXS_AUTH_STATE_HANDSHAKE_FAILED, /**< Authentication failed immediately */
    MXS_AUTH_STATE_COMPLETE /**< Authentication is complete */
} mxs_auth_state_t;

/**
 * The GWAUTHENTICATOR version data. The following should be updated whenever
 * the GWAUTHENTICATOR structure is changed. See the rules defined in modinfo.h
 * that define how these numbers should change.
 */
#define GWAUTHENTICATOR_VERSION      {1, 1, 0}


#endif /* GW_AUTHENTICATOR_H */

