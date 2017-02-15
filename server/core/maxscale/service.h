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
 * @param name  The service name
 * @param router The router module this service uses
 *
 * @return The newly created service or NULL if an error occurred
 */
SERVICE* service_alloc(const char *name, const char *router);

/**
 * @brief Free the specified service
 *
 * @param service The service to free
 */
void service_free(SERVICE *service);

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
 * @return Number of successfully started services
 */
int service_launch_all(void);

/**
 * Creating and adding new components to services
 */
SERV_LISTENER* serviceCreateListener(SERVICE *service, const char *name,
                                     const char *protocol, const char *address,
                                     unsigned short port, const char *authenticator,
                                     const char *options, SSL_LISTENER *ssl);

void serviceRemoveBackend(SERVICE *service, const SERVER *server);

/**
 * @brief Serialize a service to a file
 *
 * This partially converts @c service into an INI format file. Only the servers
 * of the service are serialized. This allows the service to keep using the servers
 * added at runtime even after a restart.
 *
 * NOTE: This does not persist the complete service configuration and requires
 * that an existing service configuration is in the main configuration file.
 * Changes to service parameters are not persisted.
 *
 * @param service Service to serialize
 * @return False if the serialization of the service fails, true if it was successful
 */
bool service_serialize_servers(const SERVICE *service);

/**
 * Internal utility functions
 */
char*    service_get_name(SERVICE* service);
bool     service_all_services_have_listeners(void);
int      service_isvalid(SERVICE *service);

/**
 * Check if a service uses @c servers
 * @param server Server that is queried
 * @return True if server is used by at least one service
 */
bool service_server_in_use(const SERVER *server);

/** Update the server weights used by services */
void service_update_weights();

/**
 * Alteration of the service configuration
 */
void  serviceAddRouterOption(SERVICE *service, char *option);
void  serviceClearRouterOptions(SERVICE *service);
void  service_update(SERVICE *service, char *router, char *user, char *auth);

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
 * Internal debugging diagnostics
 */
void       printService(SERVICE *service);
void       printAllServices(void);

MXS_END_DECLS
