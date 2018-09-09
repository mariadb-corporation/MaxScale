/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

/**
 * @file listener.h
 */

#include <maxscale/cdefs.h>
#include <maxbase/jansson.h>
#include <maxscale/protocol.h>
#include <maxscale/ssl.h>

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
    char*                 name;         /**< Name of the listener */
    char*                 protocol;     /**< Protocol module to load */
    unsigned short        port;         /**< Port to listen on */
    char*                 address;      /**< Address to listen with */
    char*                 authenticator;/**< Name of authenticator */
    char*                 auth_options; /**< Authenticator options */
    void*                 auth_instance;/**< Authenticator instance created in MXS_AUTHENTICATOR::initialize()
                                         * */
    SSL_LISTENER*         ssl;          /**< Structure of SSL data or NULL */
    struct dcb*           listener;     /**< The DCB for the listener */
    struct users*         users;        /**< The user data for this listener */
    struct service*       service;      /**< The service which used by this listener */
    SPINLOCK              lock;
    int                   active;   /**< True if the port has not been deleted */
    struct  servlistener* next;     /**< Next service protocol */
} SERV_LISTENER;                    // TODO: Rename to LISTENER

typedef struct listener_iterator
{
    SERV_LISTENER* current;
} LISTENER_ITERATOR;

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
bool listener_serialize(const SERV_LISTENER* listener);

/**
 * @brief Convert listener to JSON
 *
 * @param listener Listener to convert
 *
 * @return Converted listener
 */
json_t* listener_to_json(const SERV_LISTENER* listener);

SERV_LISTENER* listener_alloc(struct service* service,
                              const char* name,
                              const char* protocol,
                              const char* address,
                              unsigned short port,
                              const char* authenticator,
                              const char* auth_options,
                              SSL_LISTENER* ssl);
void listener_free(SERV_LISTENER* listener);
int  listener_set_ssl_version(SSL_LISTENER* ssl_listener, const char* version);
void listener_set_certificates(SSL_LISTENER* ssl_listener, char* cert, char* key, char* ca_cert);


/**
 * Initialize SSL configuration
 *
 * This sets up the generated RSA encryption keys, chooses the listener
 * encryption level and configures the listener certificate, private key and
 * certificate authority file.
 *
 * @note This function should not be called directly, use config_create_ssl() instead
 *
 * @todo Combine this with config_create_ssl() into one function
 *
 * @param ssl SSL configuration to initialize
 *
 * @return True on success, false on error
 */
bool SSL_LISTENER_init(SSL_LISTENER* ssl);

/**
 * Free an SSL_LISTENER
 *
 * @param ssl SSL_LISTENER to free
 */
void SSL_LISTENER_free(SSL_LISTENER* ssl);

/**
 * @brief Check if listener is active
 *
 * @param listener Listener to check
 *
 * @return True if listener is active
 */
bool listener_is_active(SERV_LISTENER* listener);

/**
 * @brief Modify listener active state
 *
 * @param listener Listener to modify
 * @param active True to activate, false to disable
 */
void listener_set_active(SERV_LISTENER* listener, bool active);

/**
 * @brief Initialize a listener iterator for iterating service listeners
 *
 * @param service Service whose listeners are iterated
 * @param iter    Pointer to iterator to initialize
 *
 * @return The first value pointed by the iterator
 */
SERV_LISTENER* listener_iterator_init(const struct service* service, LISTENER_ITERATOR* iter);

/**
 * @brief Get the next listener
 *
 * @param iter Listener iterator
 *
 * @return The next listener or NULL on end of list
 */
SERV_LISTENER* listener_iterator_next(LISTENER_ITERATOR* iter);

/**
 * Get listener state as a string
 *
 * @param listener Listener to inspect
 *
 * @return State of the listener as a string
 */
const char* listener_state_to_string(const SERV_LISTENER* listener);

MXS_END_DECLS
