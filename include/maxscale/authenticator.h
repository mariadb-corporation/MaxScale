#pragma once
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
 * @file authenticator.h
 *
 * The authenticator module interface definitions for MaxScale
 */

#include <maxscale/cdefs.h>

#include <maxscale/buffer.h>

MXS_BEGIN_DECLS

/** Maximum number of authenticator options */
#define AUTHENTICATOR_MAX_OPTIONS 256

struct dcb;
struct server;
struct session;
struct servlistener;

/**
 * @verbatim
 * The operations that can be performed on the descriptor
 *
 *      initialize      Initialize the authenticator instance. The return value
 *                      of this function will be given to the `create` entry point.
 *                      If a module does not implement this entry point, the value
 *                      given to the `create` is NULL.
 *
 *      create          Create a data structure unique to this DCB, stored in
 *                      `dcb->authenticator_data`. If a module does not implement
 *                      this entry point, `dcb->authenticator_data` will be set to NULL.
 *
 *      extract         Extract the data from a buffer and place in a structure
 *                      shared at the session level, stored in `dcb->data`
 *
 *      connectSSL      Determine whether the connection can support SSL
 *
 *      authenticate    Carry out the authentication
 *
 *      free            Free extracted data. This is only called for the client
 *                      side authenticators so backend authenticators should not
 *                      implement it.
 *
 *      destroy         Destroy the unique DCB data returned by the `create`
 *                      entry point.
 *
 *      loadusers       Load or update authenticator user data
 *
 *      diagnostic      Print diagnostic output to a DCB
 *
 *      reauthenticate  Reauthenticate a user
 *
 * @endverbatim
 *
 * This forms the "module object" for authenticator modules within the gateway.
 *
 * @see load_module
 */
typedef struct mxs_authenticator
{
    void* (*initialize)(char **options);
    void* (*create)(void* instance);
    int   (*extract)(struct dcb *, GWBUF *);
    bool  (*connectssl)(struct dcb *);
    int   (*authenticate)(struct dcb *);
    void  (*free)(struct dcb *);
    void  (*destroy)(void *);
    int   (*loadusers)(struct servlistener *);
    void  (*diagnostic)(struct dcb*, struct servlistener *);

    /** This entry point was added to avoid calling authenticator functions
     * directly when a COM_CHANGE_USER command is executed. */
    int (*reauthenticate)(struct dcb *, const char *user,
                          uint8_t *token, size_t token_len, /**< Client auth token */
                          uint8_t *scramble, size_t scramble_len, /**< Scramble sent by MaxScale to client */
                          uint8_t *output, size_t output_len); /**< Hashed client password used by backend protocols */
} MXS_AUTHENTICATOR;

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
#define MXS_AUTH_LOADUSERS_ERROR 1 /**< Temporary error, service is started */
#define MXS_AUTH_LOADUSERS_FATAL 2 /**< Fatal error, service is not started */

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
 * The MXS_AUTHENTICATOR version data. The following should be updated whenever
 * the MXS_AUTHENTICATOR structure is changed. See the rules defined in modinfo.h
 * that define how these numbers should change.
 */
#define MXS_AUTHENTICATOR_VERSION      {1, 1, 0}


bool authenticator_init(void **instance, const char *authenticator, const char *options);
const char* get_default_authenticator(const char *protocol);

MXS_END_DECLS
