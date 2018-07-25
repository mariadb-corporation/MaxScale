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

#include <maxscale/service.h>

MXS_BEGIN_DECLS

/**
 * @file service.h - MaxScale internal service functions
 */

/**
 * Service life cycle management
 *
 * These functions should only be called by the MaxScale core.
 */

/**
 * @brief Allocate a new service
 *
 * @param name   The service name
 * @param router The router module this service uses
 * @param params Service parameters
 *
 * @return The newly created service or NULL if an error occurred
 */
SERVICE* service_alloc(const char *name, const char *router, MXS_CONFIG_PARAMETER* params);

/**
 * Free a service
 *
 * @note Must not be called if the service has any active client connections or
 *       active listeners
 *
 * @param service Service to free
 */
void service_free(SERVICE* service);

/**
 * Mark a service for destruction
 *
 * Once the service reference count drops down to zero, the service is destroyed.
 *
 * @param service Service to destroy
 */
void service_destroy(SERVICE *service);

/**
 * Check whether a service can be destroyed
 *
 * @param service Service to check
 *
 * @return True if service can be destroyed
 */
bool service_can_be_destroyed(SERVICE *service);

/**
 * @brief Shut all services down
 *
 * Turns on the shutdown flag in each service. This should be done as
 * part of the MaxScale shutdown.
 */
void service_shutdown(void);

/**
 * @brief Destroy all service router and filter instances
 *
 * Calls the @c destroyInstance entry point of each service' router and
 * filters. This should be done after all worker threads have exited.
 */
void service_destroy_instances(void);

/**
 * @brief Launch all services
 *
 * Initialize and start all services. This should only be called once by the
 * main initialization code.
 *
 * @return Number of successfully started services or -1 on error
 */
int service_launch_all(void);

/**
 * Perform thread-specific initialization
 *
 * Currently this function only pre-loads users for all threads.
 *
 * @return True on success, false on error (currently always returns true).
 */
bool service_thread_init();

/**
 * Creating and adding new components to services
 */
SERV_LISTENER* serviceCreateListener(SERVICE *service, const char *name,
                                     const char *protocol, const char *address,
                                     unsigned short port, const char *authenticator,
                                     const char *options, SSL_LISTENER *ssl);

/**
 * @brief Remove a listener from use
 *
 * @note This does not free the memory
 *
 * @param service Service that owns the listener
 * @param char    Name of the listener to remove
 *
 * @return True if listener was found and removed
 */
bool service_remove_listener(SERVICE *service, const char* target);

void serviceRemoveBackend(SERVICE *service, const SERVER *server);

/**
 * @brief Serialize a service to a file
 *
 * This converts @c service into an INI format file.
 *
 * NOTE: This does not persist the complete service configuration and requires
 * that an existing service configuration is in the main configuration file.
 *
 * @param service Service to serialize
 * @return False if the serialization of the service fails, true if it was successful
 */
bool service_serialize(const SERVICE *service);

/**
 * Internal utility functions
 */
bool     service_all_services_have_listeners(void);
int      service_isvalid(SERVICE *service);

/**
 * Check if a service uses @c servers
 * @param server Server that is queried
 * @return True if server is used by at least one service
 */
bool service_server_in_use(const SERVER *server);

/**
 * Check if filter is used by any service
 *
 * @param filter Filter to inspect
 *
 * @return True if at least one service uses the filter
 */
bool service_filter_in_use(const MXS_FILTER_DEF *filter);

/** Update the server weights used by services */
void service_update_weights();

/**
 * @brief Add parameters to a service
 *
 * A copy of @c param is added to @c service.
 *
 * @param service Service where the parameters are added
 * @param param Parameters to add
 */
void service_add_parameters(SERVICE *service, const MXS_CONFIG_PARAMETER *param);

/**
 * @brief Add parameters to a service
 *
 * A copy of @c param is added to @c service.
 *
 * @param service Service where the parameters are added
 * @param key     Parameter name
 * @param value   Parameter value
 */
void service_add_parameter(SERVICE *service, const char* key, const char* value);

/**
 * @brief Remove service parameter
 *
 * @param service Service to modify
 * @param key     Parameter to remove
 */
void service_remove_parameter(SERVICE *service, const char* key);

/**
 * @brief Replace service parameter
 *
 * @param service Service to modify
 * @param key     Parameter name
 * @param value   Parameter value
 */
void service_replace_parameter(SERVICE *service, const char* key, const char* value);

/**
 * @brief Set listener rebinding interval
 *
 * @param service Service to configure
 * @param value String value o
 */
void service_set_retry_interval(SERVICE *service, int value);

MXS_END_DECLS
