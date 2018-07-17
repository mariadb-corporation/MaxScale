#pragma once
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
 * @file config_runtime.h  - Functions for runtime configuration modifications
 */

#include <maxscale/cdefs.h>

#include <maxscale/adminusers.h>
#include <maxscale/monitor.h>
#include <maxscale/server.h>
#include <maxscale/service.h>

MXS_BEGIN_DECLS

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
 * @return True on success, false if an error occurred
 */
bool runtime_create_server(const char *name, const char *address,
                           const char *port, const char *protocol,
                           const char *authenticator);

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
bool runtime_alter_server(SERVER *server, const char *key, const char *value);

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
bool runtime_alter_monitor(MXS_MONITOR *monitor, const char *key, const char *value);

/**
 * @brief Alter service parameters
 *
 * @param monitor Service to alter
 * @param key     Key to modify
 * @param value   New value
 *
 * @return True if @c key was one of the supported parameters
 */
bool runtime_alter_service(SERVICE *service, const char* zKey, const char* zValue);

/**
 * @brief Alter MaxScale parameters
 *
 * @param name  Key to modify
 * @param value New value
 *
 * @return True if @c key was one of the supported parameters
 */
bool runtime_alter_maxscale(const char* name, const char* value);

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
 * @brief Create a new filter
 *
 * @param name   Name of the filter
 * @param module Filter module
 * @param params Filter parameters
 *
 * @return True if a new filter was created and persisted
 */
bool runtime_create_filter(const char *name, const char *module, MXS_CONFIG_PARAMETER* params);

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

/**
 * @brief Create a new server from JSON
 *
 * @param json JSON defining the server
 *
 * @return Created server or NULL on error
 */
SERVER* runtime_create_server_from_json(json_t* json);

/**
 * @brief Alter a server using JSON
 *
 * @param server Server to alter
 * @param new_json JSON definition of the updated server
 *
 * @return True if the server was successfully modified to represent @c new_json
 */
bool runtime_alter_server_from_json(SERVER* server, json_t* new_json);

/**
 * @brief Alter server relationships
 *
 * @param server Server to alter
 * @param type Type of the relation, either @c services or @c monitors
 * @param json JSON that defines the relationship data
 *
 * @return True if the relationships were successfully modified
 */
bool runtime_alter_server_relationships_from_json(SERVER* server, const char* type, json_t* json);

/**
 * @brief Create a new monitor from JSON
 *
 * @param json JSON defining the monitor
 *
 * @return Created monitor or NULL on error
 */
MXS_MONITOR* runtime_create_monitor_from_json(json_t* json);

/**
 * @brief Create a new filter from JSON
 *
 * @param json JSON defining the filter
 *
 * @return Created filter or NULL on error
 */
MXS_FILTER_DEF* runtime_create_filter_from_json(json_t* json);

/**
 * @brief Alter a monitor using JSON
 *
 * @param monitor Monitor to alter
 * @param new_json JSON definition of the updated monitor
 *
 * @return True if the monitor was successfully modified to represent @c new_json
 */
bool runtime_alter_monitor_from_json(MXS_MONITOR* monitor, json_t* new_json);

/**
 * @brief Alter monitor relationships
 *
 * @param monitor Monitor to alter
 * @param json JSON that defines the new relationships
 *
 * @return True if the relationships were successfully modified
 */
bool runtime_alter_monitor_relationships_from_json(MXS_MONITOR* monitor, json_t* json);

/**
 * @brief Alter a service using JSON
 *
 * @param service Service to alter
 * @param new_json JSON definition of the updated service
 *
 * @return True if the service was successfully modified to represent @c new_json
 */
bool runtime_alter_service_from_json(SERVICE* service, json_t* new_json);

/**
 * @brief Alter service relationships
 *
 * @param service Service to alter
 * @param json JSON that defines the new relationships
 *
 * @return True if the relationships were successfully modified
 */
bool runtime_alter_service_relationships_from_json(SERVICE* service, json_t* json);

/**
 * @brief Create a listener from JSON
 *
 * @param service Service where the listener is created
 * @param json JSON definition of the new listener
 *
 * @return True if the listener was successfully created and started
 */
bool runtime_create_listener_from_json(SERVICE* service, json_t* json);

/**
 * @brief Alter logging options using JSON
 *
 * @param json JSON definition of the updated logging options
 *
 * @return True if the modifications were successful
 */
bool runtime_alter_logs_from_json(json_t* json);

/**
 * @brief Get current runtime error in JSON format
 *
 * @return The latest runtime error in JSON format or NULL if no error has occurred
 */
json_t* runtime_get_json_error();

/**
 * @brief Create a new user account
 *
 * @param json JSON defining the user
 *
 * @return True if the user was successfully created
 */
bool runtime_create_user_from_json(json_t* json);

/**
 * @brief Remove admin user
 *
 * @param id   Username of the network user
 * @param type USER_TYPE_INET for network user and USER_TYPE_UNIX for enabled accounts
 *
 * @return True if user was successfully removed
 */
bool runtime_remove_user(const char* id, enum user_type type);

/**
 * @brief Alter core MaxScale parameters from JSON
 *
 * @param new_json JSON defining the new core parameters
 *
 * @return True if the core parameters are valid and were successfully applied
 */
bool runtime_alter_maxscale_from_json(json_t* new_json);

MXS_END_DECLS
