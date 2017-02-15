#pragma once
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
 * @file listener.h
 */

#include <maxscale/cdefs.h>
#include <maxscale/protocol.h>
#include <maxscale/ssl.h>
#include <maxscale/hashtable.h>

MXS_BEGIN_DECLS

struct dcb;
struct service;

/**
 * The servlistener structure is used to link a service to the protocols that
 * are used to support that service. It defines the name of the protocol module
 * that should be loaded to support the client connection and the port that the
 * protocol should use to listen for incoming client connections.
 */
typedef struct servlistener
{
    char *name;                 /**< Name of the listener */
    char *protocol;             /**< Protocol module to load */
    unsigned short port;        /**< Port to listen on */
    char *address;              /**< Address to listen with */
    char *authenticator;        /**< Name of authenticator */
    char *auth_options;         /**< Authenticator options */
    void *auth_instance;        /**< Authenticator instance created in MXS_AUTHENTICATOR::initialize() */
    SSL_LISTENER *ssl;          /**< Structure of SSL data or NULL */
    struct dcb *listener;       /**< The DCB for the listener */
    struct users *users;        /**< The user data for this listener */
    struct service* service;    /**< The service which used by this listener */
    SPINLOCK lock;
    struct  servlistener *next; /**< Next service protocol */
} SERV_LISTENER;

/**
 * @brief Serialize a listener to a file
 *
 * This converts @c listener into an INI format file. This allows created listeners
 * to be persisted to disk. This will replace any existing files with the same
 * name.
 *
 * @param listener Listener to serialize
 * @return True if the serialization of the listener was successful, false if it fails
 */
bool listener_serialize(const SERV_LISTENER *listener);

SERV_LISTENER* listener_alloc(struct service* service, const char* name, const char *protocol,
                              const char *address, unsigned short port, const char *authenticator,
                              const char* auth_options, SSL_LISTENER *ssl);
void listener_free(SERV_LISTENER* listener);
int listener_set_ssl_version(SSL_LISTENER *ssl_listener, char* version);
void listener_set_certificates(SSL_LISTENER *ssl_listener, char* cert, char* key, char* ca_cert);
int listener_init_SSL(SSL_LISTENER *ssl_listener);

MXS_END_DECLS
