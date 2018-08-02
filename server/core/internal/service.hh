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
#include <maxscale/resultset.hh>

/**
 * @file service.h - MaxScale internal service functions
 */

/**
 * Service life cycle management
 *
 * These functions should only be called by the MaxScale core.
 */

// The internal service representation. Currently it only inherits the SERVICE struct.
class Service: public SERVICE
{
public:
    ~Service();
};

/**
 * @brief Allocate a new service
 *
 * @param name   The service name
 * @param router The router module this service uses
 * @param params Service parameters
 *
 * @return The newly created service or NULL if an error occurred
 */
Service* service_alloc(const char *name, const char *router, MXS_CONFIG_PARAMETER* params);

/**
 * Free a service
 *
 * @note Must not be called if the service has any active client connections or
 *       active listeners
 *
 * @param service Service to free
 */
void service_free(Service* service);

/**
 * Mark a service for destruction
 *
 * Once the service reference count drops down to zero, the service is destroyed.
 *
 * @param service Service to destroy
 */
void service_destroy(Service *service);

/**
 * Check whether a service can be destroyed
 *
 * @param service Service to check
 *
 * @return True if service can be destroyed
 */
bool service_can_be_destroyed(Service *service);

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
SERV_LISTENER* serviceCreateListener(Service *service, const char *name,
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
bool service_remove_listener(Service *service, const char* target);

void serviceRemoveBackend(Service *service, const SERVER *server);

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
bool service_serialize(const Service *service);

/**
 * Internal utility functions
 */
bool     service_all_services_have_listeners(void);
bool      service_isvalid(Service *service);

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
void service_add_parameters(Service *service, const MXS_CONFIG_PARAMETER *param);

/**
 * @brief Add parameters to a service
 *
 * A copy of @c param is added to @c service.
 *
 * @param service Service where the parameters are added
 * @param key     Parameter name
 * @param value   Parameter value
 */
void service_add_parameter(Service *service, const char* key, const char* value);

/**
 * @brief Remove service parameter
 *
 * @param service Service to modify
 * @param key     Parameter to remove
 */
void service_remove_parameter(Service *service, const char* key);

/**
 * @brief Replace service parameter
 *
 * @param service Service to modify
 * @param key     Parameter name
 * @param value   Parameter value
 */
void service_replace_parameter(Service *service, const char* key, const char* value);

/**
 * @brief Set listener rebinding interval
 *
 * @param service Service to configure
 * @param value String value o
 */
void service_set_retry_interval(Service *service, int value);

// Internal search function
Service* service_internal_find(const char *name);

// Assign filters to service
bool  service_set_filters(Service *service, const char* filters);

/**
 * @brief Check if a service uses a server
 * @param service Service to check
 * @param server Server being used
 * @return True if service uses the server
 */
bool serviceHasBackend(Service *service, SERVER *server);

/**
 * @brief Start new a listener for a service
 *
 * @param service Service where the listener is linked
 * @param port Listener to start
 * @return True if listener was started
 */
bool serviceLaunchListener(Service *service, SERV_LISTENER *port);

/**
 * @brief Find listener with specified properties.
 *
 * @param service Service to check
 * @param socket  Listener socket path
 * @param address Listener address
 * @param port    Listener port number
 *
 * @note Either socket should be NULL and port non-zero or socket
 *       non-NULL and port zero.
 *
 * @return True if service has the listener
 */
SERV_LISTENER* service_find_listener(Service* service,
                                     const char* socket,
                                     const char* address,
                                     unsigned short port);

/**
 * @brief Check if a service has a listener
 *
 * @param service Service to check
 * @param protocol Listener protocol
 * @param address Listener address
 * @param port Listener port
 * @return True if service has the listener
 */
bool serviceHasListener(Service* service, const char* name, const char* protocol,
                        const char* address, unsigned short port);

/**
 * @brief Check if a MaxScale service listens on a port
 *
 * @param port The port to check
 * @return True if a MaxScale service uses the port
 */
bool service_port_is_used(unsigned short port);

/**
 * @brief Check if the service has a listener with a matching name
 *
 * @param service Service to check
 * @param name    Name to compare to
 *
 * @return True if the service has a listener with a matching name
 */
bool service_has_named_listener(Service *service, const char *name);

// Deprecated setters
int   serviceSetUser(Service *service, const char *user, const char *auth);
int   serviceSetTimeout(Service *service, int val);
int   serviceSetConnectionLimits(Service *service, int max, int queued, int timeout);
void  serviceSetRetryOnFailure(Service *service, const char* value);
void  serviceWeightBy(Service *service, const char *weightby);
int   serviceEnableLocalhostMatchWildcardHost(Service *service, int action);
int   serviceStripDbEsc(Service* service, int action);
int   serviceAuthAllServers(Service *service, int action);
void  serviceSetVersionString(Service *service, const char* value);
int   serviceEnableRootUser(Service *service, int action);

/**
 * @brief Convert a service to JSON
 *
 * @param service Service to convert
 * @param host    Hostname of this server
 *
 * @return JSON representation of the service
 */
json_t* service_to_json(const Service* service, const char* host);

/**
 * @brief Convert all services to JSON
 *
 * @param host Hostname of this server
 *
 * @return A JSON array with all services
 */
json_t* service_list_to_json(const char* host);

/**
 * @brief Convert service listeners to JSON
 *
 * @param service Service whose listeners are converted
 * @param host    Hostname of this server
 *
 * @return Array of JSON format listeners
 */
json_t* service_listener_list_to_json(const Service* service, const char* host);

/**
 * @brief Convert service listener to JSON
 *
 * @param service Service whose listener is converted
 * @param name    The name of the listener
 * @param host    Hostname of this server
 *
 * @return JSON format listener
 */
json_t* service_listener_to_json(const Service* service, const char* name, const char* host);

/**
 * @brief Get links to services that relate to a server
 *
 * @param server Server to inspect
 * @param host   Hostname of this server
 *
 * @return Array of service links or NULL if no relations exist
 */
json_t* service_relations_to_server(const SERVER* server, const char* host);

/**
 * @brief Get links to services that relate to a filter
 *
 * @param filter Filter to inspect
 * @param host   Hostname of this server
 *
 * @return Array of service links
 */
json_t* service_relations_to_filter(const MXS_FILTER_DEF* filter, const char* host);

std::unique_ptr<ResultSet> serviceGetList(void);
std::unique_ptr<ResultSet> serviceGetListenerList(void);
