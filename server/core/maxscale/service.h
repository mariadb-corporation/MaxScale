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
 * Stops all services and calls the destroyInstance entry points for all routers
 * and filter. This should only be called once by the main shutdown code.
 */
void service_shutdown(void);

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
int  serviceHasProtocol(SERVICE *service, const char *protocol,
                        const char* address, unsigned short port);
void serviceRemoveBackend(SERVICE *service, const SERVER *server);
bool serviceHasBackend(SERVICE *service, SERVER *server);

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

/**
 * Alteration of the service configuration
 */
void  serviceAddRouterOption(SERVICE *service, char *option);
void  serviceClearRouterOptions(SERVICE *service);
void  service_update(SERVICE *service, char *router, char *user, char *auth);
bool  service_set_param_value(SERVICE* service, CONFIG_PARAMETER* param, char* valstr,
                              count_spec_t count_spec, config_param_type_t type);

/**
 * Internal debugging diagnostics
 */
void       printService(SERVICE *service);
void       printAllServices(void);

MXS_END_DECLS