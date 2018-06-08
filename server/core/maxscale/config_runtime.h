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
 * @file config_runtime.h  - Functions for runtime configuration modifications
 */

#include <maxscale/cdefs.h>

#include <maxscale/monitor.h>
#include <maxscale/server.h>
#include <maxscale/service.h>

/**
 * @brief Create a new server
 *
 * This function creates a new, persistent server by first allocating a new
 * server and then storing the resulting configuration file on disk. This
 * function should be used only from administrative interface modules and internal
 * modules should use server_alloc() instead.
 *
 * @param name          Server name
 * @param address       Network address
 * @param port          Network port
 * @param protocol      Protocol module name
 * @param authenticator Authenticator module name
 * @param options       Options for the authenticator module
 * @return True on success, false if an error occurred
 */
bool runtime_create_server(const char *name, const char *address,
                           const char *port, const char *protocol,
                           const char *authenticator, const char *options);

/**
 * @brief Destroy a server
 *
 * This removes any created server configuration files and marks the server removed
 * If the server is not in use.
 *
 * @param server Server to destroy
 * @return True if server was destroyed
 */
bool runtime_destroy_server(SERVER *server);

/**
 * @brief Link a server to an object
 *
 * This function links the server to another object. The target can be either
 * a monitor or a service.
 *
 * @param server Server to link
 * @param target The monitor or service where the server is added
 * @return True if the object was found and the server was linked to it, false
 * if no object matching @c target was found
 */
bool runtime_link_server(SERVER *server, const char *target);

/**
 * @brief Unlink a server from an object
 *
 * This function unlinks the server from another object. The target can be either
 * a monitor or a service.
 *
 * @param server Server to unlink
 * @param target The monitor or service from which the server is removed
 * @return True if the object was found and the server was unlinked from it, false
 * if no object matching @c target was found
 */
bool runtime_unlink_server(SERVER *server, const char *target);

/**
 * @brief Alter server parameters
 *
 * @param server Server to alter
 * @param key Key to modify
 * @param value New value
 * @return True if @c key was one of the supported parameters
 */
bool runtime_alter_server(SERVER *server, char *key, char *value);

/**
 * @brief Enable SSL for a server
 *
 * The @c key , @c cert and @c ca parameters are required. @c version and @c depth
 * are optional.
 *
 * @note SSL cannot be disabled at runtime.
 *
 * @param server Server to configure
 * @param key Path to SSL private key
 * @param cert Path to SSL public certificate
 * @param ca Path to certificate authority
 * @param version Required SSL Version
 * @param depth Certificate verification depth
 * @param verify Verify peer certificate
 *
 * @return True if SSL was successfully enabled
 */
bool runtime_enable_server_ssl(SERVER *server, const char *key, const char *cert,
                               const char *ca, const char *version, const char *depth,
                               const char *verify);

/**
 * @brief Alter monitor parameters
 *
 * @param monitor Monitor to alter
 * @param key Key to modify
 * @param value New value
 * @return True if @c key was one of the supported parameters
 */
bool runtime_alter_monitor(MXS_MONITOR *monitor, char *key, char *value);

/**
 * @brief Create a new listener for a service
 *
 * This function adds a new listener to a service and starts it.
 *
 * @param service     Service where the listener is added
 * @param name        Name of the listener
 * @param addr        Listening address, NULL for default of ::
 * @param port        Listening port, NULL for default of 3306
 * @param proto       Listener protocol, NULL for default of "MySQLClient"
 * @param auth        Listener authenticator, NULL for protocol default authenticator
 * @param auth_opt    Options for the authenticator, NULL for no options
 * @param ssl_key     SSL key, NULL for no key
 * @param ssl_cert    SSL cert, NULL for no cert
 * @param ssl_ca      SSL CA cert, NULL for no CA cert
 * @param ssl_version SSL version, NULL for default of "MAX"
 * @param ssl_depth   SSL cert verification depth, NULL for default
 * @param verify_ssl  SSL peer certificate verification, NULL for default
 *
 * @return True if the listener was successfully created and started
 */
bool runtime_create_listener(SERVICE *service, const char *name, const char *addr,
                             const char *port, const char *proto, const char *auth,
                             const char *auth_opt, const char *ssl_key,
                             const char *ssl_cert, const char *ssl_ca,
                             const char *ssl_version, const char *ssl_depth,
                             const char *verify_ssl);

/**
 * @brief Destroy a listener
 *
 * This disables the listener by removing it from the polling system. It also
 * removes any generated configurations for this listener.
 *
 * @param service Service where the listener exists
 * @param name Name of the listener
 *
 * @return True if the listener was successfully destroyed
 */
bool runtime_destroy_listener(SERVICE *service, const char *name);

/**
 * @brief Create a new monitor
 *
 * @param name Name of the monitor
 * @param module Monitor module
 * @return True if new monitor was created and persisted
 */
bool runtime_create_monitor(const char *name, const char *module);

/**
 * @brief Destroy a monitor
 *
 * Monitors are not removed from the runtime configuration but they are stopped.
 * Destroyed monitor are removed after a restart.
 *
 * @param monitor Monitor to destroy
 * @return True if monitor was destroyed
 */
bool runtime_destroy_monitor(MXS_MONITOR *monitor);
