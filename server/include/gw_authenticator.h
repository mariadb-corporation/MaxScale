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
} GWAUTHENTICATOR;

/** Return values for the loadusers entry point */
#define AUTH_LOADUSERS_OK    0 /**< Users loaded successfully */
#define AUTH_LOADUSERS_ERROR 1 /**< Failed to load users */

/**
 * The GWAUTHENTICATOR version data. The following should be updated whenever
 * the GWAUTHENTICATOR structure is changed. See the rules defined in modinfo.h
 * that define how these numbers should change.
 */
#define GWAUTHENTICATOR_VERSION      {1, 1, 0}


#endif /* GW_AUTHENTICATOR_H */

