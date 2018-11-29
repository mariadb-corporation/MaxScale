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

#include <maxscale/ccdefs.hh>

#include <string>
#include <memory>
#include <vector>

#include <maxbase/jansson.h>
#include <maxscale/protocol.h>
#include <maxscale/ssl.h>
#include <maxscale/service.hh>

struct dcb;
class SERVICE;

/**
 * The Listener class is used to link a network port to a service. It defines the name of the
 * protocol module that should be loaded as well as the authenticator that is used.
 */
// TODO: Rename to Listener
class SERV_LISTENER
{
public:
    SERV_LISTENER(SERVICE* service, const std::string& name, const std::string& address, uint16_t port,
                  const std::string& protocol, const std::string& authenticator,
                  const std::string& auth_opts, void* auth_instance, SSL_LISTENER* ssl);
    ~SERV_LISTENER();

public:
    std::string    name;            /**< Name of the listener */
    std::string    protocol;        /**< Protocol module to load */
    uint16_t       port;            /**< Port to listen on */
    std::string    address;         /**< Address to listen with */
    std::string    authenticator;   /**< Name of authenticator */
    std::string    auth_options;    /**< Authenticator options */
    void*          auth_instance;   /**< Authenticator instance */
    SSL_LISTENER*  ssl;             /**< Structure of SSL data or NULL */
    struct dcb*    listener;        /**< The DCB for the listener */
    struct users*  users;           /**< The user data for this listener */
    SERVICE*       service;         /**< The service which used by this listener */
    int            active;          /**< True if the port has not been deleted */
    SERV_LISTENER* next;            /**< Next service protocol */
};

using SListener = std::shared_ptr<SERV_LISTENER>;

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
bool listener_serialize(const SListener& listener);

/**
 * @brief Convert listener to JSON
 *
 * @param listener Listener to convert
 *
 * @return Converted listener
 */
json_t* listener_to_json(const SListener& listener);

/**
 * Create a new listener
 *
 * @param service       Service where the listener points to
 * @param name          Name of the listener
 * @param protocol      Protocol module to use
 * @param address       The address to listen with
 * @param port          The port to listen on
 * @param authenticator Name of the authenticator to be used
 * @param auth_options  Authenticator options
 * @param ssl           SSL configuration
 *
 * @return New listener or nullptr on error
 */
SListener listener_alloc(SERVICE* service,
                         const char* name,
                         const char* protocol,
                         const char* address,
                         unsigned short port,
                         const char* authenticator,
                         const char* auth_options,
                         SSL_LISTENER* ssl);

/**
 * @brief Free a listener
 *
 * The listener must be destroyed before it can be freed.
 *
 * @param listener Listener to free
 */
void listener_free(const SListener& listener);

/**
 * Destroy a listener
 *
 * This deactivates the listener and closes the network port it listens on. Once destroyed, the listener
 * can no longer be used.
 *
 * @param listener Listener to destroy
 */
void listener_destroy(const SListener& listener);

/**
 * Stop a listener
 *
 * @param listener Listener to stop
 *
 * @return True if listener was successfully stopped
 */
bool listener_stop(const SListener& listener);

/**
 * Start a stopped listener
 *
 * @param listener Listener to start
 *
 * @return True if listener was successfully started
 */
bool listener_start(const SListener& listener);

/**
 * Find a listener
 *
 * @param name Name of the listener
 *
 * @return The listener if it exists or an empty SListener if it doesn't
 */
SListener listener_find(const std::string& name);

/**
 * Find all listeners that point to a service
 *
 * @param service Service whose listeners are returned
 *
 * @return The listeners that point to the service
 */
std::vector<SListener> listener_find_by_service(const SERVICE* service);

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
bool listener_is_active(const SListener& listener);

/**
 * @brief Modify listener active state
 *
 * @param listener Listener to modify
 * @param active True to activate, false to disable
 */
void listener_set_active(const SListener& listener, bool active);

/**
 * Get listener state as a string
 *
 * @param listener Listener to inspect
 *
 * @return State of the listener as a string
 */
const char* listener_state_to_string(const SListener& listener);
