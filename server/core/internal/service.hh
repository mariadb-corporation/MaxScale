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

#include <maxscale/service.h>
#include <maxscale/resultset.hh>

#include <mutex>
#include <string>
#include <vector>

#include "filter.hh"

/**
 * @file service.h - MaxScale internal service functions
 */

struct LastUserLoad
{
    time_t last = 0;        // The last time the users were loaded
    bool   warned = false;  // Has a warning been logged
};

// The internal service representation
class Service : public SERVICE
{
public:
    using FilterList = std::vector<SFilterDef>;
    using RateLimits = std::vector<LastUserLoad>;

    Service(const std::string& name, const std::string& router, MXS_CONFIG_PARAMETER* params);

    ~Service();

    /**
     * Check if name matches a basic service parameter
     *
     * Basic parameters are common to all services. These include, for example, the
     * `user` and `password` parameters.
     *
     * @return True if the parameter is a basic service parameter
     */
    bool is_basic_parameter(const std::string& name);

    /**
     * Update a basic service parameter
     *
     * Update a parameter that is common to all services.
     *
     * @param name    Name of the parameter to update
     * @param value   The new value of the parameter
     */
    void update_basic_parameter(const std::string& name, const std::string& value);

    /**
     * Set the list of filters for this service
     *
     * @param filters Filters to set
     *
     * @return True if filters were all found and were valid
     */
    bool set_filters(const std::vector<std::string>& filters);

    /**
     * Get the list of filters this service uses
     *
     * @note This can lock the service if this is the first time this worker
     *       accesses the filter list
     *
     * @return A list of filters or an empty list of no filters are in use
     */
    const FilterList& get_filters() const;

    /**
     * Reload users for all listeners
     *
     * @return True if loading of users was successful
     */
    bool refresh_users();

    /**
     * Dump service configuration into a file
     *
     * @param filename File where the configuration should be written
     *
     * @return True on success
     */
    bool dump_config(const char* filename) const;

    // TODO: Make JSON output internal (could iterate over get_filters() but that takes the service lock)
    json_t* json_relationships(const char* host) const;

    // TODO: Make these private
    mutable std::mutex lock;

private:
    FilterList  m_filters;          /**< Ordered list of filters */
    std::string m_name;             /**< Name of the service */
    std::string m_router_name;      /**< Router module */
    std::string m_user;             /**< Username */
    std::string m_password;         /**< Password */
    std::string m_weightby;         /**< Weighting parameter name */
    std::string m_version_string;   /**< Version string sent to clients */
    RateLimits  m_rate_limits;      /**< The refresh rate limits for users of each thread */
    uint64_t    m_wkey;             /**< Key for worker local data */

    // Get the worker local filter list
    FilterList* get_local_filters() const;

    // Update the local filter list on the current worker
    void update_local_filters();

    // Callback for updating the local filter list
    static void update_filters_cb(void* data)
    {
        Service* service = static_cast<Service*>(data);
        service->update_local_filters();
    }
};

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
Service* service_alloc(const char* name, const char* router, MXS_CONFIG_PARAMETER* params);

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
void service_destroy(Service* service);

/**
 * Check whether a service can be destroyed
 *
 * @param service Service to check
 *
 * @return True if service can be destroyed
 */
bool service_can_be_destroyed(Service* service);

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
SERV_LISTENER* serviceCreateListener(Service* service,
                                     const char* name,
                                     const char* protocol,
                                     const char* address,
                                     unsigned short port,
                                     const char* authenticator,
                                     const char* options,
                                     SSL_LISTENER* ssl);

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
bool service_remove_listener(Service* service, const char* target);

void serviceRemoveBackend(Service* service, const SERVER* server);

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
bool service_serialize(const Service* service);

/**
 * Internal utility functions
 */
bool service_all_services_have_listeners(void);
bool service_isvalid(Service* service);

/**
 * Check if a service uses @c servers
 * @param server Server that is queried
 * @return True if server is used by at least one service
 */
bool service_server_in_use(const SERVER* server);

/**
 * Check if filter is used by any service
 *
 * @param filter Filter to inspect
 *
 * @return True if at least one service uses the filter
 */
bool service_filter_in_use(const SFilterDef& filter);

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
void service_add_parameters(Service* service, const MXS_CONFIG_PARAMETER* param);

/**
 * @brief Add parameters to a service
 *
 * A copy of @c param is added to @c service.
 *
 * @param service Service where the parameters are added
 * @param key     Parameter name
 * @param value   Parameter value
 */
void service_add_parameter(Service* service, const char* key, const char* value);

/**
 * @brief Remove service parameter
 *
 * @param service Service to modify
 * @param key     Parameter to remove
 */
void service_remove_parameter(Service* service, const char* key);

/**
 * @brief Replace service parameter
 *
 * @param service Service to modify
 * @param key     Parameter name
 * @param value   Parameter value
 */
void service_replace_parameter(Service* service, const char* key, const char* value);

// Internal search function
Service* service_internal_find(const char* name);

/**
 * @brief Check if a service uses a server
 * @param service Service to check
 * @param server Server being used
 * @return True if service uses the server
 */
bool serviceHasBackend(Service* service, SERVER* server);

/**
 * @brief Start new a listener for a service
 *
 * @param service Service where the listener is linked
 * @param port Listener to start
 * @return True if listener was started
 */
bool serviceLaunchListener(Service* service, SERV_LISTENER* port);

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
bool serviceHasListener(Service* service,
                        const char* name,
                        const char* protocol,
                        const char* address,
                        unsigned short port);

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
bool service_has_named_listener(Service* service, const char* name);

// Required by MaxAdmin
int service_enable_root(Service* service, int action);

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
json_t* service_relations_to_filter(const SFilterDef& filter, const char* host);

std::unique_ptr<ResultSet> serviceGetList(void);
std::unique_ptr<ResultSet> serviceGetListenerList(void);
