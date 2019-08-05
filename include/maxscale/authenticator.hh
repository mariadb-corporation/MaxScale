/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

/**
 * @file authenticator.hh
 *
 * The authenticator module interface definitions for MaxScale
 */

#include <maxscale/ccdefs.hh>

class Listener;
class SERVER;
struct DCB;
struct GWBUF;
struct json_t;
struct MXS_SESSION;

namespace maxscale
{
class Authenticator;
}

/**
 * The MXS_AUTHENTICATOR version data. The following should be updated whenever
 * the MXS_AUTHENTICATOR structure is changed. See the rules defined in modinfo.h
 * that define how these numbers should change.
 */
#define MXS_AUTHENTICATOR_VERSION {2, 1, 0}

/** Maximum number of authenticator options */
#define AUTHENTICATOR_MAX_OPTIONS 256

/** Return values for extract and authenticate entry points */
#define MXS_AUTH_SUCCEEDED             0/**< Authentication was successful */
#define MXS_AUTH_FAILED                1/**< Authentication failed */
#define MXS_AUTH_FAILED_DB             2/**< Authentication failed, database not found */
#define MXS_AUTH_FAILED_SSL            3/**< SSL authentication failed */
#define MXS_AUTH_INCOMPLETE            4/**< Authentication is not yet complete */
#define MXS_AUTH_SSL_INCOMPLETE        5/**< SSL connection is not yet complete */
#define MXS_AUTH_SSL_COMPLETE          6/**< SSL connection complete or not required */
#define MXS_AUTH_NO_SESSION            7
#define MXS_AUTH_BAD_HANDSHAKE         8/**< Malformed client packet */
#define MXS_AUTH_FAILED_WRONG_PASSWORD 9/**< Client provided wrong password */

/** Return values for the loadusers entry point */
#define MXS_AUTH_LOADUSERS_OK    0  /**< Users loaded successfully */
#define MXS_AUTH_LOADUSERS_ERROR 1  /**< Temporary error, service is started */
#define MXS_AUTH_LOADUSERS_FATAL 2  /**< Fatal error, service is not started */

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
 * @endverbatim
 *
 * This forms the "module object" for authenticator modules within the gateway.
 *
 * @see load_module
 */
struct MXS_AUTHENTICATOR
{
    void* (* initialize)(char** options);
    void* (* create)(void* instance);
};

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
enum mxs_auth_state_t
{
    MXS_AUTH_STATE_INIT,            /**< Initial authentication state */
    MXS_AUTH_STATE_PENDING_CONNECT, /**< Connection creation is underway */
    MXS_AUTH_STATE_CONNECTED,       /**< Network connection to server created */
    MXS_AUTH_STATE_MESSAGE_READ,    /**< Read a authentication message from the server */
    MXS_AUTH_STATE_RESPONSE_SENT,   /**< Responded to the read authentication message */
    MXS_AUTH_STATE_FAILED,          /**< Authentication failed */
    MXS_AUTH_STATE_HANDSHAKE_FAILED,/**< Authentication failed immediately */
    MXS_AUTH_STATE_COMPLETE         /**< Authentication is complete */
};

mxs::Authenticator* authenticator_init(const char* authenticator, const char* options);
const char* get_default_authenticator(const char* protocol);

namespace maxscale
{

const char* to_string(mxs_auth_state_t state);
}
