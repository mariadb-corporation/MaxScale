/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/service.hh>
#include <maxscale/resultset.hh>
#include <maxscale/router.hh>

#include <mutex>
#include <string>
#include <vector>

#include "filter.hh"

namespace maxscale
{
class Monitor;
}

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

    const std::vector<mxs::Target*>& get_children() const override
    {
        return m_data->targets;
    }

    uint64_t status() const override;

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

    // TODO: Make this private.
    mxs::Monitor* m_monitor {nullptr};      /**< A possibly associated monitor */

    bool uses_cluster() const
    {
        return m_monitor != nullptr;
    }

    uint64_t get_version(service_version_which_t which) const
    {
        return which == SERVICE_VERSION_MAX ? m_data->version_max : m_data->version_min;
    }

    std::unique_ptr<mxs::Endpoint> get_connection(mxs::Component* up, MXS_SESSION* session) override;

    int64_t replication_lag() const override;

    // Adds a routing target to this service
    void add_target(mxs::Target* target);

    // Removes a target
    void remove_target(mxs::Target* target);

    bool has_target(mxs::Target* target) const
    {
        return std::find(m_data->targets.begin(), m_data->targets.end(), target) != m_data->targets.end();
    }

    const Config& config() const override
    {
        return *m_config;
    }

    std::vector<SERVER*> reachable_servers() const final
    {
        return m_data->servers;
    }

    /**
     * Check whether a service can be destroyed
     *
     * @return True if service can be destroyed
     */
    bool can_be_destroyed() const;

    const MXS_CONFIG_PARAMETER& params() const override
    {
        return m_params;
    }

    void remove_parameter(const std::string& key)
    {
        m_params.remove(key);
    }

    void set_parameter(const std::string& key, const std::string& value)
    {
        m_params.set(key, value);
    }

private:

    struct Data
    {
        // Server version numbers, precalculated
        uint64_t   version_max {std::numeric_limits<uint64_t>::max()};
        uint64_t   version_min {0};
        FilterList filters;     // Ordered list of filters

        // List of servers this service reaches via its direct descendants. All servers are leaf nodes but not
        // all leaf nodes are servers. As the list of servers is relatively often required and the
        // construction is somewhat costly, the values are precalculated whenever the list of direct
        // descendants is updated (i.e. the targets of the service).
        std::vector<SERVER*> servers;

        // The targets that this service points to i.e. the children of this node in the routing tree.
        std::vector<mxs::Target*> targets;
    };

    mxs::rworker_local<Data>   m_data;
    RateLimits                 m_rate_limits;   // User reload rate limits
    mxs::rworker_local<Config> m_config;

    MXS_CONFIG_PARAMETER m_params;

    /**
     * Recalculate internal data
     *
     * Recalculates the server reach this service has as well as the minimum and maximum server versions
     * available through this service.
     */
    void targets_updated();
};

// A connection to a service
class ServiceEndpoint final : public mxs::Endpoint
{
public:
    ServiceEndpoint(MXS_SESSION* session, Service* service, mxs::Component* up);
    ~ServiceEndpoint();

    mxs::Target* target() const override;

    bool connect() override;

    void close() override;

    bool is_open() const override;

    int32_t routeQuery(GWBUF* buffer) override;

    int32_t clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    bool handleError(GWBUF* error, mxs::Endpoint* down, const mxs::Reply& reply) override;

private:

    // Class that holds the session specific filter data (TODO: Remove duplicate from session.cc)
    class SessionFilter
    {
    public:

        SessionFilter(const SFilterDef& f)
            : filter(f)
            , instance(filter->filter)
            , session(nullptr)
        {
        }

        SFilterDef          filter;
        MXS_FILTER*         instance;
        MXS_FILTER_SESSION* session;
        mxs::Upstream       up;
        mxs::Downstream     down;
    };

    friend class Service;

    static int32_t upstream_function(MXS_FILTER*, MXS_FILTER_SESSION*, GWBUF*,
                                     const mxs::ReplyRoute&, const mxs::Reply&);
    int32_t send_upstream(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply&);
    void    set_endpoints(std::vector<std::unique_ptr<mxs::Endpoint>> down);

    bool                m_open {false};
    mxs::Component*     m_up;       // The upstream where replies are routed to
    MXS_SESSION*        m_session;  // The owning session
    Service*            m_service;  // The service where the connection points to
    MXS_ROUTER_SESSION* m_router_session {nullptr};

    mxs::Downstream m_head;
    mxs::Upstream   m_tail;

    std::vector<SessionFilter> m_filters;

    // Downstream components where this component routes to
    std::vector<std::unique_ptr<mxs::Endpoint>> m_down;
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
 * @return False if a fatal error occurred
 */
bool service_launch_all(void);

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
SListener service_find_listener(Service* service,
                                const std::string& socket,
                                const std::string& address,
                                unsigned short port);

/**
 * @brief Check if a MaxScale service listens on a port
 *
 * @param port The port to check
 * @return True if a MaxScale service uses the port
 */
bool service_port_is_used(int port);

/**
 * @brief Check if a MaxScale service listens on a Unix domain socket
 *
 * @param path The socket path to check
 * @return True if a MaxScale service uses the socket
 */
bool service_socket_is_used(const std::string& socket_path);

/**
 * @brief Check if the service has a listener with a matching name
 *
 * @param service Service to check
 * @param name    Name to compare to
 *
 * @return True if the service has a listener with a matching name
 */
bool service_has_named_listener(Service* service, const char* name);

/**
 * See if a monitor is used by any service
 *
 * @param monitor Monitor to look for
 *
 * @return The first service that uses the monitor or nullptr if no service uses it
 */
Service* service_uses_monitor(mxs::Monitor* monitor);

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

/**
 * @brief Add server to all services associated with a monitor
 *
 * @param monitor  A monitor.
 * @param server   A server.
 */
void service_add_server(mxs::Monitor* pMonitor, SERVER* pServer);

/**
 * @brief Remove server from all services associated with a monitor
 *
 * @param monitor  A monitor.
 * @param server   A server.
 */
void service_remove_server(mxs::Monitor* pMonitor, SERVER* pServer);

std::unique_ptr<ResultSet> serviceGetList(void);
std::unique_ptr<ResultSet> serviceGetListenerList(void);
const MXS_MODULE_PARAM*    common_service_params();
