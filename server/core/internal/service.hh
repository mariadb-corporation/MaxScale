/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/service.hh>
#include <maxscale/router.hh>
#include <maxscale/workerlocal.hh>

#include <mutex>
#include <string>
#include <vector>

#include "filter.hh"

namespace maxscale
{
class Monitor;
}

class Listener;

/**
 * @file service.h - MaxScale internal service functions
 */

constexpr char CN_CONNECTION_KEEPALIVE[] = "connection_keepalive";
constexpr char CN_CONNECTION_TIMEOUT[] = "connection_timeout";
constexpr char CN_DISABLE_SESCMD_HISTORY[] = "disable_sescmd_history";
constexpr char CN_MAX_SESCMD_HISTORY[] = "max_sescmd_history";
constexpr char CN_NET_WRITE_TIMEOUT[] = "net_write_timeout";
constexpr char CN_PRUNE_SESCMD_HISTORY[] = "prune_sescmd_history";

// The internal service representation
class Service : public SERVICE
{
public:
    using FilterList = std::vector<SFilterDef>;
    using SAccountManager = std::unique_ptr<mxs::UserAccountManager>;
    using SAccountCache = std::unique_ptr<mxs::UserAccountCache>;

    /**
     * Find a service by name
     *
     * @param Name of the service to find
     *
     * @return Pointer to service or nullptr if not found
     */
    static Service* find(const std::string& name);

    /**
     * @brief Allocate a new service
     *
     * @param name   The service name
     * @param router The router module this service uses
     * @param params Service parameters
     *
     * @return The newly created service or NULL if an error occurred
     */
    static Service* create(const char* name, const char* router, const mxs::ConfigParameters& params);

    /**
     * Destroy a service
     *
     * Deletes the service after all client connections have been closed.
     *
     * @param service Service to destroy
     */
    static void destroy(Service* service);

    ~Service();

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
     * Remove a filter from a service
     *
     * @param filter Filter to remove
     */
    void remove_filter(SFilterDef filters);

    const std::vector<mxs::Target*>& get_children() const override
    {
        return m_data->targets;
    }

    uint64_t status() const override;

    /**
     * Persist service configuration into a stream
     *
     * @param filename Stream where the configuration is written
     *
     * @return The output stream
     */
    std::ostream& persist(std::ostream& os) const;

    // TODO: Make JSON output internal (could iterate over get_filters() but that takes the service lock)
    json_t* json_relationships(const char* host) const;

    json_t* json_parameters() const;

    static mxs::config::Specification* specification();

    // Configure service from given JSON parameters
    bool configure(json_t* params);

    // TODO: Make these private
    mutable std::mutex lock;

    // Get the current cluster
    mxs::Monitor* cluster() const
    {
        return m_monitor;
    }

    // Set the current cluster without updating targets
    void set_cluster(mxs::Monitor* monitor);

    // Removes the cluster from use (if it's used) and updates the targest
    bool remove_cluster(mxs::Monitor* monitor);

    // Changes the current cluster and updates the targets
    bool change_cluster(mxs::Monitor* monitor);

    uint64_t get_version(service_version_which_t which) const
    {
        auto versions = get_versions(m_data->servers);
        return which == SERVICE_VERSION_MAX ? versions.second : versions.first;
    }

    std::unique_ptr<mxs::Endpoint> get_connection(mxs::Component* up, MXS_SESSION* session) override;

    int64_t rank() const override
    {
        return config()->rank;
    }

    int64_t  replication_lag() const override;
    uint64_t gtid_pos(uint32_t domain) const override;
    int64_t  ping() const override;

    uint64_t capabilities() const override
    {
        return m_capabilities | m_data->target_capabilities;
    }

    // Adds a routing target to this service
    void add_target(mxs::Target* target);

    // Removes a target
    void remove_target(mxs::Target* target);

    bool has_target(mxs::Target* target) const
    {
        return std::find(m_data->targets.begin(), m_data->targets.end(), target) != m_data->targets.end();
    }

    const mxs::WorkerGlobal<Config::Values>& config() const override
    {
        return m_config.values();
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

    void incref();

    void decref();

    bool active() const override
    {
        return m_active;
    }

    const mxs::UserAccountCache* user_account_cache() const override;

    void request_user_account_update() override;
    void sync_user_account_caches() override;

    mxs::UserAccountManager* user_account_manager();

    /**
     * Set the user account manager for a service to match the given protocol. If the service already
     * has a compatible account manager, nothing needs to be done.
     *
     * @param protocol_module The protocol whose user account manager the service should use
     * @param listener Name of associated listener. Used for logging.
     * @return True on success or if existing user manager is already compatible
     */
    bool check_update_user_account_manager(mxs::ProtocolModule* protocol_module, const std::string& listener);

    void mark_for_wakeup(mxs::ClientConnection* session) override;
    void unmark_for_wakeup(mxs::ClientConnection* session) override;

private:

    struct Data
    {
        FilterList filters;     // Ordered list of filters

        // List of servers this service reaches via its direct descendants. All servers are leaf nodes but not
        // all leaf nodes are servers. As the list of servers is relatively often required and the
        // construction is somewhat costly, the values are precalculated whenever the list of direct
        // descendants is updated (i.e. the targets of the service).
        std::vector<SERVER*> servers;

        // The targets that this service points to i.e. the children of this node in the routing tree.
        std::vector<mxs::Target*> targets;

        // Combined capabilities of all of the services that this service connects to
        uint64_t target_capabilities {0};
    };

    Service(const std::string& name, const std::string& router);

    /**
     * Recalculate internal data
     *
     * Recalculates the server reach this service has as well as the minimum and maximum server versions
     * available through this service.
     */
    void targets_updated();
    void wakeup_sessions_waiting_userdata();
    void set_start_user_account_manager(SAccountManager user_manager);

    // Helper for calculating version values
    std::pair<uint64_t, uint64_t> get_versions(const std::vector<SERVER*>& servers) const;

    bool post_configure() override;

    mxs::WorkerGlobal<Data> m_data;
    Config                  m_config;
    std::atomic<int64_t>    m_refcount {1};
    bool                    m_active {true};
    mxs::Monitor*           m_monitor {nullptr};    /**< A possibly associated monitor */

    // User account manager. Can only be set once.
    SAccountManager m_usermanager;

    /** User account cache local to each worker. Each worker must initialize their own copy
     *  and update it when the master data changes. */
    mxs::WorkerLocal<SAccountCache, mxs::DefaultConstructor<SAccountCache>> m_usercache;

    /** Thread-local set of client connections waiting for updated user account data */
    mxs::WorkerLocal<std::unordered_set<mxs::ClientConnection*>> m_sleeping_clients;
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

    bool handleError(mxs::ErrorType type, GWBUF* error, mxs::Endpoint* down,
                     const mxs::Reply& reply) override;

private:

    // Class that holds the session specific filter data (TODO: Remove duplicate from session.cc)
    class SessionFilter
    {
    public:

        SessionFilter(const SFilterDef& f)
            : filter(f)
            , instance(filter->instance())
            , session(nullptr)
        {
        }

        SFilterDef          filter;
        mxs::Filter*        instance;
        mxs::FilterSession* session;
        mxs::Routable*      up;
        mxs::Routable*      down;
    };

    class ServiceUpstream : public mxs::Routable
    {
    public:
        ServiceUpstream(ServiceEndpoint* endpoint)
            : m_endpoint(endpoint)
        {
        }

        int32_t routeQuery(GWBUF* pPacket)
        {
            mxb_assert_message(false, "Should never be called");
            return 0;
        }

        int32_t clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply)
        {
            return m_endpoint->send_upstream(pPacket, down, reply);
        }

    private:
        ServiceEndpoint* m_endpoint;
    };

    friend class Service;

    static int32_t upstream_function(mxs::Filter*, mxs::Routable*, GWBUF*,
                                     const mxs::ReplyRoute&, const mxs::Reply&);
    int32_t send_upstream(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply&);
    void    set_endpoints(std::vector<std::unique_ptr<mxs::Endpoint>> down);

    bool                m_open {false};
    mxs::Component*     m_up;       // The upstream where replies are routed to
    MXS_SESSION*        m_session;  // The owning session
    Service*            m_service;  // The service where the connection points to
    mxs::RouterSession* m_router_session {nullptr};

    ServiceUpstream m_upstream;

    mxs::Routable* m_head;
    mxs::Routable* m_tail;

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
 * Internal utility functions
 */
bool service_all_services_have_listeners(void);
bool service_isvalid(Service* service);

/**
 * Check if a service uses @c servers
 *
 * @param server Server that is queried
 *
 * @return List of services that use the server
 */
std::vector<Service*> service_server_in_use(const SERVER* server);

/**
 * Check if filter is used by any service
 *
 * @param filter Filter to inspect
 *
 * @return List of services that use the filter
 */
std::vector<Service*> service_filter_in_use(const SFilterDef& filter);

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
std::shared_ptr<Listener>
service_find_listener(Service* service, const std::string& socket, const std::string& address,
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
 * @return The list of services that use the monitor
 */
std::vector<Service*> service_uses_monitor(mxs::Monitor* monitor);

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
json_t* service_relations_to_server(const SERVER* server, const std::string& host, const std::string& self);

/**
 * @brief Get links to services that relate to a filter
 *
 * @param filter Filter to inspect
 * @param host   Hostname of this server
 *
 * @return Array of service links
 */
json_t* service_relations_to_filter(const FilterDef* filter, const std::string& host,
                                    const std::string& self);

/**
 * @brief Get links to services that relate to a monitor
 *
 * @param filter Monitor to inspect
 * @param host   Hostname of this server
 * @param self   The self link to add to the relationship
 *
 * @return Array of service links or nullptr if no service uses the monitor
 */
json_t* service_relations_to_monitor(const mxs::Monitor* monitor, const std::string& host,
                                     const std::string& self);

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
