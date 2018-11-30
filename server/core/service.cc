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
 * @file service.c  - A representation of a service within MaxScale
 */

#include <maxscale/ccdefs.hh>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>
#include <fcntl.h>
#include <atomic>
#include <map>
#include <string>
#include <set>
#include <vector>
#include <unordered_set>

#include <maxbase/atomic.hh>
#include <maxbase/jansson.h>

#include <maxscale/service.hh>
#include <maxscale/alloc.h>
#include <maxscale/dcb.h>
#include <maxscale/paths.h>
#include <maxscale/housekeeper.h>
#include <maxscale/listener.hh>
#include <maxscale/log.h>
#include <maxscale/poll.h>
#include <maxscale/protocol.h>
#include <maxscale/resultset.hh>
#include <maxscale/router.hh>
#include <maxscale/server.hh>
#include <maxscale/session.hh>
#include <maxscale/users.h>
#include <maxscale/utils.h>
#include <maxscale/utils.hh>
#include <maxscale/version.h>
#include <maxscale/json_api.h>
#include <maxscale/routingworker.h>
#include <maxscale/routingworker.hh>

#include "internal/config.hh"
#include "internal/filter.hh"
#include "internal/modules.hh"
#include "internal/service.hh"
#include "internal/maxscale.hh"

/** This define is needed in CentOS 6 systems */
#if !defined (UINT64_MAX)
#define UINT64_MAX (18446744073709551615UL)
#endif

using std::string;
using std::set;
using namespace maxscale;
using LockGuard = std::lock_guard<std::mutex>;
using UniqueLock = std::unique_lock<std::mutex>;

static struct
{
    std::mutex            lock;
    std::vector<Service*> services;
} this_unit;

static bool service_internal_restart(void* data);
static void service_calculate_weights(SERVICE* service);

Service* service_alloc(const char* name, const char* router, MXS_CONFIG_PARAMETER* params)
{
    MXS_ROUTER_OBJECT* router_api = (MXS_ROUTER_OBJECT*)load_module(router, MODULE_ROUTER);

    if (router_api == NULL)
    {
        MXS_ERROR("Unable to load router module '%s'", router);
        return NULL;
    }

    Service* service = new(std::nothrow) Service(name, router, params);

    if (service == nullptr)
    {
        MXS_OOM();
        return NULL;
    }

    if (service->conn_idle_timeout)
    {
        dcb_enable_session_timeouts();
    }

    // Store router, used when service is serialized
    service_add_parameter(service, CN_ROUTER, router);

    // Store parameters in the service
    service_add_parameters(service, params);

    service->router_instance = router_api->createInstance(service, params);

    if (service->router_instance == NULL)
    {
        MXS_ERROR("%s: Failed to create router instance. Service not started.", service->name);
        service->active = false;
        delete service;
        return NULL;
    }

    if (router_api->getCapabilities)
    {
        service->capabilities |= router_api->getCapabilities(service->router_instance);
    }

    LockGuard guard(this_unit.lock);
    this_unit.services.push_back(service);

    return service;
}

static std::string get_version_string(MXS_CONFIG_PARAMETER* params)
{
    std::string version_string = config_get_string(params, CN_VERSION_STRING);

    if (!version_string.empty() && version_string[0] != '5')
    {
        /**
         * Add the 5.5.5- string to the start of the version string if the version
         * string starts with "10.".  This mimics MariaDB 10.0 which adds 5.5.5-
         * for backwards compatibility.
         */
        version_string = "5.5.5-" + version_string;
    }

    return version_string;
}

Service::Service(const std::string& service_name,
                 const std::string& router_name,
                 MXS_CONFIG_PARAMETER* params)
    : m_name(service_name)
    , m_router_name(router_name)
    , m_user(config_get_string(params, CN_USER))
    , m_password(config_get_string(params, CN_PASSWORD))
    , m_weightby(config_get_string(params, CN_WEIGHTBY))
    , m_version_string(get_version_string(params))
    , m_rate_limits(config_threadcount())
    , m_wkey(mxs_rworker_create_key())
{
    const MXS_MODULE* module = get_module(router_name.c_str(), MODULE_ROUTER);
    mxb_assert(module);
    mxb_assert(load_module(router_name.c_str(), MODULE_ROUTER) == module->module_object);

    router = (MXS_ROUTER_OBJECT*)module->module_object;
    capabilities = module->module_capabilities;
    client_count = 0;
    n_dbref = 0;
    // TODO: Remove once the name and router module are not directly visible to modules
    name = m_name.c_str();
    routerModule = m_router_name.c_str();
    svc_config_param = NULL;
    svc_config_version = 0;
    stats.started = time(0);
    stats.n_failed_starts = 0;
    stats.n_current = 0;
    stats.n_sessions = 0;
    state = SERVICE_STATE_ALLOC;
    active = true;
    dbref = NULL;
    n_dbref = 0;
    snprintf(user, sizeof(user), "%s", m_user.c_str());
    snprintf(password, sizeof(password), "%s", m_password.c_str());
    snprintf(weightby, sizeof(weightby), "%s", m_weightby.c_str());
    snprintf(version_string, sizeof(version_string), "%s", m_version_string.c_str());

    max_retry_interval = config_get_integer(params, CN_MAX_RETRY_INTERVAL);
    users_from_all = config_get_bool(params, CN_AUTH_ALL_SERVERS);
    localhost_match_wildcard_host = config_get_bool(params, CN_LOCALHOST_MATCH_WILDCARD_HOST);
    retry_start = config_get_bool(params, CN_RETRY_ON_FAILURE);
    enable_root = config_get_bool(params, CN_ENABLE_ROOT_USER);
    conn_idle_timeout = config_get_integer(params, CN_CONNECTION_TIMEOUT);
    max_connections = config_get_integer(params, CN_MAX_CONNECTIONS);
    log_auth_warnings = config_get_bool(params, CN_LOG_AUTH_WARNINGS);
    strip_db_esc = config_get_bool(params, CN_STRIP_DB_ESC);
    session_track_trx_state = config_get_bool(params, CN_SESSION_TRACK_TRX_STATE);

    if (config_get_param(params, CN_RETAIN_LAST_STATEMENTS))
    {
        retain_last_statements = config_get_integer(params, CN_RETAIN_LAST_STATEMENTS);
    }
    else
    {
        retain_last_statements = -1;    // Indicates that it has not been set.
    }

    /**
     * At service start last update is set to config->users_refresh_time seconds earlier.
     * This way MaxScale could try reloading users just after startup. But only if user
     * refreshing has not been turned off.
     */
    MXS_CONFIG* cnf = config_get_global_options();

    // Defaults for disabled reloading of users
    bool warned = true;
    bool last = time(NULL);

    if (cnf->users_refresh_time != INT32_MAX)
    {
        last -= cnf->users_refresh_time;
        warned = false;
    }

    for (auto& a : m_rate_limits)
    {
        a.last = last;
        a.warned = warned;
    }
}

Service::~Service()
{
    mxs_rworker_delete_data(m_wkey);

    if (router && router_instance && router->destroyInstance)
    {
        router->destroyInstance(router_instance);
    }

    while (SERVER_REF* tmp = dbref)
    {
        mxb_assert(!tmp->active || maxscale_teardown_in_progress());
        dbref = dbref->next;
        MXS_FREE(tmp);
    }

    config_parameter_free(svc_config_param);
}

void service_free(Service* service)
{
    mxb_assert(mxb::atomic::load(&service->client_count) == 0 || maxscale_teardown_in_progress());
    mxb_assert(!service->active || maxscale_teardown_in_progress());

    {
        LockGuard guard(this_unit.lock);
        auto it = std::remove(this_unit.services.begin(), this_unit.services.end(), service);
        mxb_assert(it != this_unit.services.end());
        this_unit.services.erase(it);
    }

    delete service;
}

void service_destroy(Service* service)
{
#ifdef SS_DEBUG
    auto current = mxs::RoutingWorker::get_current();
    auto main = mxs::RoutingWorker::get(mxs::RoutingWorker::MAIN);
    mxb_assert_message(current == main, "Destruction of service must be done on the main worker");
#endif

    mxb_assert(service->active);
    atomic_store_int(&service->active, false);

    char filename[PATH_MAX + 1];
    snprintf(filename,
             sizeof(filename),
             "%s/%s.cnf",
             get_config_persistdir(),
             service->name);

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove persisted service configuration at '%s': %d, %s",
                  filename,
                  errno,
                  mxs_strerror(errno));
    }

    if (mxb::atomic::load(&service->client_count) == 0)
    {
        // The service has no active sessions, it can be closed immediately
        service_free(service);
    }
}

/**
 * Check to see if a service pointer is valid
 *
 * @param service       The pointer to check
 * @return 1 if the service is in the list of all services
 */
bool service_isvalid(Service* service)
{
    LockGuard guard(this_unit.lock);
    return std::find(this_unit.services.begin(),
                     this_unit.services.end(),
                     service) != this_unit.services.end();
}

/**
 * Start an individual port/protocol pair
 *
 * @param service       The service
 * @param port          The port to start
 * @return              The number of listeners started
 */
static int serviceStartPort(Service* service, const SListener& port)
{
    mxb_assert(service && service->router && service->router_instance);
    return port->listen();
}

/**
 * Start all ports for a service.
 * serviceStartAllPorts will try to start all listeners associated with the service.
 * If no listeners are started, the starting of ports will be retried after a period of time.
 * @param service Service to start
 * @return Number of started listeners. This is equal to the number of ports the service
 * is listening to.
 */
int serviceStartAllPorts(Service* service)
{
    int listeners = 0;
    auto my_listeners = listener_find_by_service(service);

    if (!my_listeners.empty())
    {
        for (const auto& listener : my_listeners)
        {
            if (maxscale_is_shutting_down())
            {
                break;
            }

            listeners += serviceStartPort(service, listener);
        }

        if (service->state == SERVICE_STATE_FAILED)
        {
            listeners = 0;
        }
        else if (listeners)
        {
            service->state = SERVICE_STATE_STARTED;
            service->stats.started = time(0);
        }
        else if (service->retry_start)
        {
            /** Service failed to start any ports. Try again later. */
            service->stats.n_failed_starts++;
            char taskname[strlen(service->name) + strlen("_start_retry_")
                          + (int) ceil(log10(INT_MAX)) + 1];
            int retry_after = MXS_MIN(service->stats.n_failed_starts * 10, service->max_retry_interval);
            snprintf(taskname,
                     sizeof(taskname),
                     "%s_start_retry_%d",
                     service->name,
                     service->stats.n_failed_starts);
            hktask_add(taskname, service_internal_restart, service, retry_after);
            MXS_NOTICE("Failed to start service %s, retrying in %d seconds.",
                       service->name,
                       retry_after);

            /** This will prevent MaxScale from shutting down if service start is retried later */
            listeners = 1;
        }
    }
    else
    {
        MXS_WARNING("Service '%s' has no listeners defined.", service->name);
        listeners = 1;      /** Set this to one to suppress errors */
    }

    return listeners;
}

/**
 * Start a service
 *
 * This function loads the protocol modules for each port on which the
 * service listens and starts the listener on that port
 *
 * @param service The Service that should be started
 *
 * @return Returns the number of listeners created
 */
int serviceInitialize(Service* service)
{
    /** Calculate the server weights */
    service_calculate_weights(service);

    int listeners = 0;

    if (!config_get_global_options()->config_check)
    {
        listeners = serviceStartAllPorts(service);
    }
    else
    {
        /** We're only checking that the configuration is valid */
        listeners++;
    }

    return listeners;
}

bool serviceLaunchListener(Service* service, const SListener& port)
{
    mxb_assert(service->state != SERVICE_STATE_FAILED);
    bool rval = true;
    LockGuard guard(service->lock);

    if (serviceStartPort(service, port) == 0)
    {
        /** Failed to start the listener */
        rval = false;
    }


    return rval;
}

bool serviceStopListener(SERVICE* svc, const char* name)
{
    auto listener = listener_find(name);
    return listener && listener->service() == svc && listener->stop();
}

bool serviceStartListener(SERVICE* svc, const char* name)
{
    auto listener = listener_find(name);
    return listener && listener->service() == svc && listener->start();
}

int service_launch_all()
{
    int n = 0, i;
    bool error = false;
    int num_svc = this_unit.services.size();

    MXS_NOTICE("Starting a total of %d services...", num_svc);

    int curr_svc = 1;
    for (Service* service : this_unit.services)
    {
        n += (i = serviceInitialize(service));
        MXS_NOTICE("Service '%s' started (%d/%d)", service->name, curr_svc++, num_svc);

        if (i == 0)
        {
            MXS_ERROR("Failed to start service '%s'.", service->name);
            error = true;
        }

        if (maxscale_is_shutting_down())
        {
            break;
        }
    }

    return error ? -1 : n;
}

bool serviceStop(SERVICE* service)
{
    int listeners = 0;

    if (service)
    {
        for (const auto& listener : listener_find_by_service(service))
        {
            if (listener->stop())
            {
                listeners++;
            }
        }

        service->state = SERVICE_STATE_STOPPED;
    }

    return listeners > 0;
}

/**
 * Restart a service
 *
 * This function stops the listener for the service
 *
 * @param service       The Service that should be restarted
 * @return      Returns the number of listeners restarted
 */
bool serviceStart(SERVICE* service)
{
    int listeners = 0;

    if (service)
    {
        for (const auto& listener : listener_find_by_service(service))
        {
            if (listener->start())
            {
                listeners++;
            }
        }

        service->state = SERVICE_STATE_STARTED;
    }

    return listeners > 0;
}

bool service_remove_listener(Service* service, const char* target)
{
    bool rval = false;
    auto listener = listener_find(target);

    if (listener && listener->service() == service)
    {
        Listener::destroy(listener);
        rval = true;
    }

    return rval;
}

SListener service_find_listener(Service* service,
                                const std::string& socket,
                                const std::string& address,
                                unsigned short port)
{
    SListener rval;

    for (const auto& listener : listener_find_by_service(service))
    {
        if (port == listener->port() && (listener->address() == address || listener->address() == socket))
        {
            rval = listener;
            break;
        }
    }

    return rval;
}

bool service_has_named_listener(Service* service, const char* name)
{
    auto listener = listener_find(name);
    return listener && listener->service() == service;
}

bool service_can_be_destroyed(Service* service)
{
    bool rval = listener_find_by_service(service).empty();

    if (rval)
    {
        for (auto s = service->dbref; s; s = s->next)
        {
            if (s->active)
            {
                rval = false;
                break;
            }
        }
    }

    if (!service->get_filters().empty())
    {
        rval = false;
    }

    return rval;
}

/**
 * Allocate a new server reference
 *
 * @param server Server to refer to
 * @return Server reference or NULL on error
 */
static SERVER_REF* server_ref_create(SERVER* server)
{
    SERVER_REF* sref = (SERVER_REF*)MXS_MALLOC(sizeof(SERVER_REF));

    if (sref)
    {
        sref->next = NULL;
        sref->server = server;
        // all servers have weight 1.0, when weights are not configured.
        sref->server_weight = 1.0;
        sref->connections = 0;
        sref->active = true;
    }

    return sref;
}

/**
 * Add a backend database server to a service
 *
 * @param service       The service to add the server to
 * @param server        The server to add
 */
bool serviceAddBackend(SERVICE* svc, SERVER* server)
{
    Service* service = static_cast<Service*>(svc);
    bool rval = false;

    if (!serviceHasBackend(service, server))
    {
        SERVER_REF* new_ref = server_ref_create(server);

        if (new_ref)
        {
            rval = true;
            LockGuard guard(service->lock);

            service->n_dbref++;

            if (service->dbref)
            {
                SERVER_REF* ref = service->dbref;
                SERVER_REF* prev = ref;

                while (ref)
                {
                    if (ref->server == server)
                    {
                        ref->active = true;
                        break;
                    }
                    prev = ref;
                    ref = ref->next;
                }

                if (ref == NULL)
                {
                    /** A new server that hasn't been used by this service */
                    atomic_synchronize();
                    prev->next = new_ref;
                }
                else
                {
                    MXS_FREE(new_ref);
                }
            }
            else
            {
                atomic_synchronize();
                service->dbref = new_ref;
            }
        }
    }

    return rval;
}

/**
 * @brief Remove a server from a service
 *
 * This function sets the server reference into an inactive state. This does not
 * remove the server from the list or free any of the memory.
 *
 * @param service Service to modify
 * @param server  Server to remove
 */
void serviceRemoveBackend(Service* service, const SERVER* server)
{
    LockGuard guard(service->lock);

    for (SERVER_REF* ref = service->dbref; ref; ref = ref->next)
    {
        if (ref->server == server && ref->active)
        {
            ref->active = false;
            service->n_dbref--;
            break;
        }
    }
}

/**
 * Test if a server is part of a service
 *
 * @param service       The service to add the server to
 * @param server        The server to add
 * @return              Non-zero if the server is already part of the service
 */
bool serviceHasBackend(Service* service, SERVER* server)
{
    SERVER_REF* ptr;

    LockGuard guard(service->lock);
    ptr = service->dbref;
    while (ptr)
    {
        if (ptr->server == server && ptr->active)
        {
            break;
        }
        ptr = ptr->next;
    }

    return ptr != NULL;
}

/**
 * Get the service user that is used to log in to the backend servers
 * associated with this service.
 *
 * @param service       The service we are setting the data for
 * @param user          The user name to use for connections
 * @param auth          The authentication data we need, e.g. MySQL SHA1 password
 */
void serviceGetUser(SERVICE* svc, const char** user, const char** auth)
{
    Service* service = static_cast<Service*>(svc);
    *user = service->user;
    *auth = service->password;
}

/**
 * Enable/Disable root user for this service
 * associated with this service.
 *
 * @param service       The service we are setting the data for
 * @param action        1 for root enable, 0 for disable access
 * @return              0 on failure
 */

int service_enable_root(Service* svc, int action)
{
    Service* service = static_cast<Service*>(svc);

    if (action != 0 && action != 1)
    {
        return 0;
    }

    service->enable_root = action;

    return 1;
}

bool Service::set_filters(const std::vector<std::string>& filters)
{
    bool rval = true;
    std::vector<SFilterDef> flist;
    uint64_t my_capabilities = 0;

    for (auto f : filters)
    {
        fix_object_name(f);

        if (auto def = filter_find(f.c_str()))
        {
            flist.push_back(def);

            const MXS_MODULE* module = get_module(def->module.c_str(), MODULE_FILTER);
            mxb_assert(module);
            my_capabilities |= module->module_capabilities;

            if (def->obj->getCapabilities)
            {
                my_capabilities |= def->obj->getCapabilities(def->filter);
            }
        }
        else
        {
            MXS_ERROR("Unable to find filter '%s' for service '%s'", f.c_str(), name);
            rval = false;
        }
    }

    if (rval)
    {
        UniqueLock guard(lock);
        m_filters = flist;
        capabilities |= my_capabilities;
        guard.unlock();

        // Broadcast a message to other workers to update their filter lists
        mxs_rworker_broadcast(update_filters_cb, this);
    }

    return rval;
}

static void destroy_filter_list(void* data)
{
    Service::FilterList* filters = static_cast<Service::FilterList*>(data);
    delete filters;
}

Service::FilterList* Service::get_local_filters() const
{
    FilterList* filters = static_cast<FilterList*>(mxs_rworker_get_data(m_wkey));

    if (filters == nullptr)
    {
        UniqueLock guard(lock);
        filters = new FilterList(m_filters);
        guard.unlock();
        mxs_rworker_set_data(m_wkey, filters, destroy_filter_list);
    }

    return filters;
}

void Service::update_local_filters()
{
    FilterList* filters = get_local_filters();
    LockGuard guard(lock);
    *filters = m_filters;
}

const Service::FilterList& Service::get_filters() const
{
    return *get_local_filters();
}

void Service::add_listener(const SListener& listener)
{
    m_listeners.push_back(listener);
}

void Service::remove_listener(const SListener& listener)
{
    m_listeners.erase(std::remove(m_listeners.begin(), m_listeners.end(), listener), m_listeners.end());
}

Service* service_internal_find(const char* name)
{
    LockGuard guard(this_unit.lock);

    for (Service* s : this_unit.services)
    {
        if (strcmp(s->name, name) == 0 && atomic_load_int(&s->active))
        {
            return s;
        }
    }

    return nullptr;
}

/**
 * Return a named service
 *
 * @param servname      The name of the service to find
 * @return The service or NULL if not found
 */
SERVICE* service_find(const char* servname)
{
    return service_internal_find(servname);
}

/**
 * Print all services to a DCB
 *
 * Designed to be called within a CLI command in order
 * to display all active services within the gateway
 */
void dprintAllServices(DCB* dcb)
{
    LockGuard guard(this_unit.lock);

    for (Service* s : this_unit.services)
    {
        dprintService(dcb, s);
    }
}

/**
 * Print details of a single service.
 *
 * @param dcb           DCB to print data to
 * @param service       The service to print
 */
void dprintService(DCB* dcb, SERVICE* svc)
{
    Service* service = static_cast<Service*>(svc);
    SERVER_REF* server = service->dbref;
    struct tm result;
    char timebuf[30];

    dcb_printf(dcb, "\tService:                             %s\n", service->name);
    dcb_printf(dcb, "\tRouter:                              %s\n", service->routerModule);
    switch (service->state)
    {
    case SERVICE_STATE_STARTED:
        dcb_printf(dcb, "\tState:                               Started\n");
        break;

    case SERVICE_STATE_STOPPED:
        dcb_printf(dcb, "\tState:                               Stopped\n");
        break;

    case SERVICE_STATE_FAILED:
        dcb_printf(dcb, "\tState:                               Failed\n");
        break;

    case SERVICE_STATE_ALLOC:
        dcb_printf(dcb, "\tState:                               Allocated\n");
        break;
    }
    if (service->router && service->router_instance)
    {
        service->router->diagnostics(service->router_instance, dcb);
    }
    dcb_printf(dcb,
               "\tStarted:                             %s",
               asctime_r(localtime_r(&service->stats.started, &result), timebuf));
    dcb_printf(dcb,
               "\tRoot user access:                    %s\n",
               service->enable_root ? "Enabled" : "Disabled");
    auto filters = service->get_filters();

    if (!filters.empty())
    {
        dcb_printf(dcb, "\tFilter chain:                ");
        const char* sep = "";
        for (const auto& f : filters)
        {
            dcb_printf(dcb, "%s %s ", f->name.c_str(), sep);
            sep = "|";
        }
        dcb_printf(dcb, "\n");
    }
    dcb_printf(dcb, "\tBackend databases:\n");
    while (server)
    {
        if (SERVER_REF_IS_ACTIVE(server))
        {
            dcb_printf(dcb,
                       "\t\t[%s]:%d    Protocol: %s    Name: %s\n",
                       server->server->address,
                       server->server->port,
                       server->server->protocol,
                       server->server->name);
        }
        server = server->next;
    }
    if (*service->weightby)
    {
        dcb_printf(dcb,
                   "\tRouting weight parameter:            %s\n",
                   service->weightby);
    }

    dcb_printf(dcb,
               "\tTotal connections:                   %d\n",
               service->stats.n_sessions);
    dcb_printf(dcb,
               "\tCurrently connected:                 %d\n",
               service->stats.n_current);
}

/**
 * List the defined services in a tabular format.
 *
 * @param dcb           DCB to print the service list to.
 */
void dListServices(DCB* dcb)
{
    const char HORIZ_SEPARATOR[] = "--------------------------+-------------------"
                                   "+--------+----------------+-------------------\n";
    LockGuard guard(this_unit.lock);

    if (!this_unit.services.empty())
    {
        dcb_printf(dcb, "Services.\n");
        dcb_printf(dcb, "%s", HORIZ_SEPARATOR);
        dcb_printf(dcb,
                   "%-25s | %-17s | #Users | Total Sessions | Backend databases\n",
                   "Service Name",
                   "Router Module");
        dcb_printf(dcb, "%s", HORIZ_SEPARATOR);

        for (Service* service : this_unit.services)
        {
            mxb_assert(service->stats.n_current >= 0);
            dcb_printf(dcb,
                       "%-25s | %-17s | %6d | %14d | ",
                       service->name,
                       service->routerModule,
                       service->stats.n_current,
                       service->stats.n_sessions);

            SERVER_REF* server_ref = service->dbref;
            bool first = true;
            while (server_ref)
            {
                if (SERVER_REF_IS_ACTIVE(server_ref))
                {
                    if (first)
                    {
                        dcb_printf(dcb, "%s", server_ref->server->name);
                    }
                    else
                    {
                        dcb_printf(dcb, ", %s", server_ref->server->name);
                    }
                    first = false;
                }
                server_ref = server_ref->next;
            }
            dcb_printf(dcb, "\n");
        }
        dcb_printf(dcb, "%s\n", HORIZ_SEPARATOR);
    }
}

/**
 * List the defined listeners in a tabular format.
 *
 * @param dcb           DCB to print the service list to.
 */
void dListListeners(DCB* dcb)
{
    LockGuard guard(this_unit.lock);

    if (!this_unit.services.empty())
    {
        dcb_printf(dcb, "Listeners.\n");
        dcb_printf(dcb,
                   "---------------------+---------------------+"
                   "--------------------+-----------------+-------+--------\n");
        dcb_printf(dcb,
                   "%-20s | %-19s | %-18s | %-15s | Port  | State\n",
                   "Name",
                   "Service Name",
                   "Protocol Module",
                   "Address");
        dcb_printf(dcb,
                   "---------------------+---------------------+"
                   "--------------------+-----------------+-------+--------\n");
    }
    for (Service* service : this_unit.services)
    {
        for (const auto& listener : listener_find_by_service(service))
        {
            dcb_printf(dcb,
                       "%-20s | %-19s | %-18s | %-15s | %5d | %s\n",
                       listener->name(),
                       service->name,
                       listener->protocol(),
                       (listener && *listener->address()) ? listener->address() : "*",
                       listener->port(),
                       listener->state());
        }
    }
    if (!this_unit.services.empty())
    {
        dcb_printf(dcb,
                   "---------------------+---------------------+"
                   "--------------------+-----------------+-------+--------\n\n");
    }
}

bool Service::refresh_users()
{
    mxs::WatchdogWorkaround workaround;
    bool ret = true;
    int self = mxs_rworker_get_current_id();
    mxb_assert(self >= 0);
    time_t now = time(NULL);

    // Use unique_lock instead of lock_guard to make the locking conditional
    UniqueLock guard(lock, std::defer_lock);

    if ((capabilities & ACAP_TYPE_ASYNC) == 0)
    {
        // Use only one rate limitation for synchronous authenticators to keep
        // rate limitations synchronous as well
        self = 0;
        guard.lock();
    }

    MXS_CONFIG* config = config_get_global_options();

    /* Check if refresh rate limit has been exceeded */
    if (now < m_rate_limits[self].last + config->users_refresh_time)
    {
        if (!m_rate_limits[self].warned)
        {
            MXS_WARNING("[%s] Refresh rate limit (once every %ld seconds) exceeded for "
                        "load of users' table.",
                        m_name.c_str(),
                        config->users_refresh_time);
            m_rate_limits[self].warned = true;
        }
    }
    else
    {
        m_rate_limits[self].last = now;
        m_rate_limits[self].warned = false;

        for (const auto& listener : listener_find_by_service(this))
        {
            /** Load the authentication users before before starting the listener */

            switch (listener->load_users())
            {
            case MXS_AUTH_LOADUSERS_FATAL:
                MXS_ERROR("[%s] Fatal error when loading users for listener '%s',"
                          " authentication will not work.",
                          m_name.c_str(),
                          listener->name());
                ret = false;
                break;

            case MXS_AUTH_LOADUSERS_ERROR:
                MXS_WARNING("[%s] Failed to load users for listener '%s', authentication"
                            " might not work.",
                            m_name.c_str(),
                            listener->name());
                ret = false;
                break;

            default:
                break;
            }
        }
    }

    return ret;
}

/**
 * Refresh the database users for the service
 * This function replaces the MySQL users used by the service with the latest
 * version found on the backend servers. There is a limit on how often the users
 * can be reloaded and if this limit is exceeded, the reload will fail.
 * @param service Service to reload
 * @return 0 on success and 1 on error
 */
int service_refresh_users(SERVICE* svc)
{
    Service* service = static_cast<Service*>(svc);
    mxb_assert(service);
    return service->refresh_users() ? 0 : 1;
}

void service_add_parameters(Service* service, const MXS_CONFIG_PARAMETER* param)
{
    while (param)
    {
        MXS_CONFIG_PARAMETER* new_param = config_clone_param(param);
        new_param->next = service->svc_config_param;
        service->svc_config_param = new_param;
        param = param->next;
    }
}

void service_add_parameter(Service* service, const char* key, const char* value)
{
    MXS_CONFIG_PARAMETER p {const_cast<char*>(key), const_cast<char*>(value), nullptr};
    service_add_parameters(service, &p);
}

void service_remove_parameter(Service* service, const char* key)
{
    if (MXS_CONFIG_PARAMETER* params = service->svc_config_param)
    {
        MXS_CONFIG_PARAMETER* to_free = NULL;

        if (strcasecmp(params->name, key) == 0)
        {
            service->svc_config_param = params->next;
            to_free = params;
        }
        else
        {
            while (MXS_CONFIG_PARAMETER* p = params->next)
            {
                if (strcasecmp(p->name, key) == 0)
                {
                    params->next = p->next;
                    to_free = p;
                    break;
                }

                params = p;
            }
        }

        if (to_free)
        {
            // Set next pointer to null to prevent freeing of other parameters
            to_free->next = NULL;
            config_parameter_free(to_free);
        }
    }
}

void service_replace_parameter(Service* service, const char* key, const char* value)
{
    for (MXS_CONFIG_PARAMETER* p = service->svc_config_param; p; p = p->next)
    {
        if (strcasecmp(p->name, key) == 0)
        {
            MXS_FREE(p->value);
            p->value = MXS_STRDUP_A(value);
            return;
        }
    }

    service_add_parameter(service, key, value);
}

/**
 * Return the parameter the wervice shoudl use to weight connections
 * by
 * @param service               The Service pointer
 */
const char* serviceGetWeightingParameter(SERVICE* svc)
{
    Service* service = static_cast<Service*>(svc);
    return service->weightby;
}

void service_destroy_instances(void)
{
    // The global list is modified by service_free so we need a copy of it
    std::vector<Service*> my_services = this_unit.services;

    for (Service* s : my_services)
    {
        service_free(s);
    }
}

/**
 * Return the count of all sessions active for all services
 *
 * @return Count of all active sessions
 */
int serviceSessionCountAll()
{
    int rval = 0;
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        rval += service->stats.n_current;
    }

    return rval;
}

/**
 * Return a resultset that has the current set of services in it
 *
 * @return A Result set
 */
std::unique_ptr<ResultSet> serviceGetListenerList()
{
    std::unique_ptr<ResultSet> set = ResultSet::create({"Service Name", "Protocol Module", "Address", "Port",
                                                        "State"});
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        for (const auto& listener : listener_find_by_service(service))
        {
            set->add_row({service->name, listener->protocol(), listener->address(),
                          std::to_string(listener->port()), listener->state()});
        }
    }

    return set;
}

/**
 * Return a result set that has the current set of services in it
 *
 * @return A Result set
 */
std::unique_ptr<ResultSet> serviceGetList()
{
    std::unique_ptr<ResultSet> set = ResultSet::create({"Service Name", "Router Module", "No. Sessions",
                                                        "Total Sessions"});
    LockGuard guard(this_unit.lock);

    for (Service* s : this_unit.services)
    {
        set->add_row({s->name, s->routerModule, std::to_string(s->stats.n_current),
                      std::to_string(s->stats.n_sessions)});
    }

    return set;
}

/**
 * Function called by the housekeeper thread to retry starting of a service
 * @param data Service to restart
 */
static bool service_internal_restart(void* data)
{
    Service* service = (Service*)data;
    serviceStartAllPorts(service);
    return false;
}

/**
 * Check that all services have listeners
 * @return True if all services have listeners
 */
bool service_all_services_have_listeners()
{
    bool rval = true;
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        if (listener_find_by_service(service).empty())
        {
            MXS_ERROR("Service '%s' has no listeners.", service->name);
            rval = false;
        }
    }

    return rval;
}
static void service_calculate_weights(SERVICE* service)
{
    const char* weightby = serviceGetWeightingParameter(service);

    if (*weightby && service->dbref)
    {
        // DEPRECATED in 2.3, remove in 2.4.
        MXS_WARNING("Setting of server weights (%s) has been deprecated"
                    " and will be removed in a later version of MaxScale.",
                    weightby);

        char buf[50];   // Enough to hold most numbers
        /** Service has a weighting parameter and at least one server */
        double total {0};

        /** Calculate total weight */
        for (SERVER_REF* server = service->dbref; server; server = server->next)
        {
            if (server_get_parameter(server->server, weightby, buf, sizeof(buf)))
            {
                long w = atol(buf);
                if (w > 0)
                {
                    total += w;
                }
            }
        }

        if (total == 0)
        {
            MXS_WARNING("Weighting parameters for service '%s' will be ignored as "
                        "no servers have (positive) values for the parameter '%s'.",
                        service->name,
                        weightby);
        }
        else
        {
            /** Calculate the relative weight of the servers */
            for (SERVER_REF* server = service->dbref; server; server = server->next)
            {
                if (server_get_parameter(server->server, weightby, buf, sizeof(buf)))
                {
                    long config_weight = atol(buf);
                    if (config_weight <= 0)
                    {
                        MXS_WARNING("Weighting parameter '%s' is set to %ld for server '%s'."
                                    " The runtime weight will be set to 0, and the server"
                                    " will only be used if no other servers are available.",
                                    weightby,
                                    config_weight,
                                    server->server->name);
                        config_weight = 0;
                    }
                    server->server_weight = config_weight / total;
                }
                else
                {
                    MXS_WARNING("Weighting parameter '%s' is not set for server '%s'."
                                " The runtime weight will be set to 0, and the server"
                                " will only be used if no other servers are available.",
                                weightby,
                                server->server->name);
                    server->server_weight = 0;
                }
            }
        }
    }
}

void service_update_weights()
{
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        service_calculate_weights(service);
    }
}

bool service_server_in_use(const SERVER* server)
{
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        LockGuard guard(service->lock);

        for (SERVER_REF* ref = service->dbref; ref; ref = ref->next)
        {
            if (ref->active && ref->server == server)
            {
                return true;
            }
        }
    }

    return false;
}

bool service_filter_in_use(const SFilterDef& filter)
{
    mxb_assert(filter);
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        for (const auto& f : service->get_filters())
        {
            if (filter == f)
            {
                return true;
            }
        }
    }

    return false;
}

/**
 * Creates a service configuration at the location pointed by @c filename
 *
 * @param service Service to serialize into a configuration
 * @param filename Filename where configuration is written
 * @return True on success, false on error
 */
bool Service::dump_config(const char* filename) const
{
    int file = open(filename, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (file == -1)
    {
        MXS_ERROR("Failed to open file '%s' when serializing service '%s': %d, %s",
                  filename,
                  m_name.c_str(),
                  errno,
                  mxs_strerror(errno));
        return false;
    }

    /**
     * TODO: Check for return values on all of the dprintf calls
     */
    dprintf(file, "[%s]\n", m_name.c_str());
    dprintf(file, "%s=service\n", CN_TYPE);

    if (!m_filters.empty())
    {
        dprintf(file, "%s=", CN_FILTERS);
        const char* sep = "";

        for (const auto& f : m_filters)
        {
            dprintf(file, "%s%s", sep, f->name.c_str());
            sep = ",";
        }

        dprintf(file, "\n");
    }

    if (dbref)
    {
        dprintf(file, "%s=", CN_SERVERS);
        const char* sep = "";

        for (SERVER_REF* db = dbref; db; db = db->next)
        {
            if (SERVER_REF_IS_ACTIVE(db))
            {
                dprintf(file, "%s%s", sep, db->server->name);
                sep = ",";
            }
        }
        dprintf(file, "\n");
    }

    const MXS_MODULE* mod = get_module(m_router_name.c_str(), NULL);
    mxb_assert(mod);

    dump_param_list(file,
                    svc_config_param,
                    {CN_TYPE, CN_FILTERS, CN_SERVERS},
                    config_service_params,
                    mod->parameters);

    close(file);

    return true;
}

bool service_serialize(const Service* service)
{
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename,
             sizeof(filename),
             "%s/%s.cnf.tmp",
             get_config_persistdir(),
             service->name);

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary service configuration at '%s': %d, %s",
                  filename,
                  errno,
                  mxs_strerror(errno));
    }
    else if (service->dump_config(filename))
    {
        char final_filename[PATH_MAX];
        strcpy(final_filename, filename);

        char* dot = strrchr(final_filename, '.');
        mxb_assert(dot);
        *dot = '\0';

        if (rename(filename, final_filename) == 0)
        {
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to rename temporary service configuration at '%s': %d, %s",
                      filename,
                      errno,
                      mxs_strerror(errno));
        }
    }

    return rval;
}

void service_print_users(DCB* dcb, const SERVICE* service)
{
    for (const auto& listener : listener_find_by_service(service))
    {
        listener->print_users(dcb);
    }
}

bool service_port_is_used(unsigned short port)
{
    bool rval = false;
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        for (const auto& listener : listener_find_by_service(service))
        {
            if (listener->port() == port)
            {
                rval = true;
                break;
            }
        }

        if (rval)
        {
            break;
        }
    }

    return rval;
}

static const char* service_state_to_string(int state)
{
    switch (state)
    {
    case SERVICE_STATE_STARTED:
        return "Started";

    case SERVICE_STATE_STOPPED:
        return "Stopped";

    case SERVICE_STATE_FAILED:
        return "Failed";

    case SERVICE_STATE_ALLOC:
        return "Allocated";

    default:
        mxb_assert(false);
        return "Unknown";
    }
}

json_t* service_parameters_to_json(const SERVICE* service)
{
    json_t* rval = json_object();

    const MXS_MODULE* mod = get_module(service->routerModule, MODULE_ROUTER);
    config_add_module_params_json(service->svc_config_param,
                                  {CN_TYPE, CN_ROUTER, CN_SERVERS, CN_FILTERS},
                                  config_service_params,
                                  mod->parameters,
                                  rval);

    return rval;
}

static inline bool have_active_servers(const SERVICE* service)
{
    for (SERVER_REF* ref = service->dbref; ref; ref = ref->next)
    {
        if (SERVER_REF_IS_ACTIVE(ref))
        {
            return true;
        }
    }

    return false;
}

static json_t* service_all_listeners_json_data(const SERVICE* service)
{
    json_t* arr = json_array();

    for (const auto& listener : listener_find_by_service(service))
    {
        json_array_append_new(arr, listener->to_json());
    }

    return arr;
}

static json_t* service_listener_json_data(const SERVICE* service, const char* name)
{
    auto listener = listener_find(name);

    if (listener && listener->service() == service)
    {
        return listener->to_json();
    }

    return NULL;
}

json_t* service_attributes(const SERVICE* service)
{
    json_t* attr = json_object();

    json_object_set_new(attr, CN_ROUTER, json_string(service->routerModule));
    json_object_set_new(attr, CN_STATE, json_string(service_state_to_string(service->state)));

    if (service->router && service->router_instance && service->router->diagnostics_json)
    {
        json_t* diag = service->router->diagnostics_json(service->router_instance);

        if (diag)
        {
            json_object_set_new(attr, CN_ROUTER_DIAGNOSTICS, diag);
        }
    }

    struct tm result;
    char timebuf[30];

    asctime_r(localtime_r(&service->stats.started, &result), timebuf);
    trim(timebuf);

    json_object_set_new(attr, "started", json_string(timebuf));
    json_object_set_new(attr, "total_connections", json_integer(service->stats.n_sessions));
    json_object_set_new(attr, "connections", json_integer(service->stats.n_current));

    /** Add service parameters and listeners */
    json_object_set_new(attr, CN_PARAMETERS, service_parameters_to_json(service));
    json_object_set_new(attr, CN_LISTENERS, service_all_listeners_json_data(service));

    return attr;
}

json_t* Service::json_relationships(const char* host) const
{
    /** Store relationships to other objects */
    json_t* rel = json_object();

    if (!m_filters.empty())
    {
        json_t* filters = mxs_json_relationship(host, MXS_JSON_API_FILTERS);

        for (const auto& f : m_filters)
        {
            mxs_json_add_relation(filters, f->name.c_str(), CN_FILTERS);
        }

        json_object_set_new(rel, CN_FILTERS, filters);
    }

    if (have_active_servers(this))
    {
        json_t* servers = mxs_json_relationship(host, MXS_JSON_API_SERVERS);

        for (SERVER_REF* ref = dbref; ref; ref = ref->next)
        {
            if (SERVER_REF_IS_ACTIVE(ref))
            {
                mxs_json_add_relation(servers, ref->server->name, CN_SERVERS);
            }
        }

        json_object_set_new(rel, CN_SERVERS, servers);
    }

    return rel;
}

json_t* service_json_data(const SERVICE* svc, const char* host)
{
    const Service* service = static_cast<const Service*>(svc);
    json_t* rval = json_object();
    LockGuard guard(service->lock);

    json_object_set_new(rval, CN_ID, json_string(service->name));
    json_object_set_new(rval, CN_TYPE, json_string(CN_SERVICES));
    json_object_set_new(rval, CN_ATTRIBUTES, service_attributes(service));
    json_object_set_new(rval, CN_RELATIONSHIPS, service->json_relationships(host));
    json_object_set_new(rval, CN_LINKS, mxs_json_self_link(host, CN_SERVICES, service->name));

    return rval;
}

json_t* service_to_json(const Service* service, const char* host)
{
    string self = MXS_JSON_API_SERVICES;
    self += service->name;
    return mxs_json_resource(host, self.c_str(), service_json_data(service, host));
}

json_t* service_listener_list_to_json(const Service* service, const char* host)
{
    /** This needs to be done here as the listeners are sort of sub-resources
     * of the service. */
    string self = MXS_JSON_API_SERVICES;
    self += service->name;
    self += "/listeners";

    return mxs_json_resource(host, self.c_str(), service_all_listeners_json_data(service));
}

json_t* service_listener_to_json(const Service* service, const char* name, const char* host)
{
    /** This needs to be done here as the listeners are sort of sub-resources
     * of the service. */
    string self = MXS_JSON_API_SERVICES;
    self += service->name;
    self += "/listeners/";
    self += name;

    return mxs_json_resource(host, self.c_str(), service_listener_json_data(service, name));
}

json_t* service_list_to_json(const char* host)
{
    json_t* arr = json_array();
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        json_t* svc = service_json_data(service, host);

        if (svc)
        {
            json_array_append_new(arr, svc);
        }
    }

    return mxs_json_resource(host, MXS_JSON_API_SERVICES, arr);
}

json_t* service_relations_to_filter(const SFilterDef& filter, const char* host)
{
    json_t* rel = mxs_json_relationship(host, MXS_JSON_API_SERVICES);
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        for (const auto& f : service->get_filters())
        {
            if (f == filter)
            {
                mxs_json_add_relation(rel, service->name, CN_SERVICES);
            }
        }
    }

    return rel;
}


json_t* service_relations_to_server(const SERVER* server, const char* host)
{
    std::vector<std::string> names;
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        LockGuard guard(service->lock);

        for (SERVER_REF* ref = service->dbref; ref; ref = ref->next)
        {
            if (ref->server == server && SERVER_REF_IS_ACTIVE(ref))
            {
                names.push_back(service->name);
            }
        }
    }

    json_t* rel = NULL;

    if (!names.empty())
    {
        rel = mxs_json_relationship(host, MXS_JSON_API_SERVICES);

        for (std::vector<std::string>::iterator it = names.begin();
             it != names.end(); it++)
        {
            mxs_json_add_relation(rel, it->c_str(), CN_SERVICES);
        }
    }

    return rel;
}

uint64_t service_get_version(const SERVICE* svc, service_version_which_t which)
{
    const Service* service = static_cast<const Service*>(svc);
    uint64_t version = 0;

    if (which == SERVICE_VERSION_ANY)
    {
        SERVER_REF* sref = service->dbref;

        while (sref && !sref->active)
        {
            sref = sref->next;
        }

        if (sref)
        {
            version = server_get_version(sref->server);
        }
    }
    else
    {
        size_t n = 0;

        uint64_t v;

        if (which == SERVICE_VERSION_MIN)
        {
            v = UINT64_MAX;
        }
        else
        {
            mxb_assert(which == SERVICE_VERSION_MAX);

            v = 0;
        }

        SERVER_REF* sref = service->dbref;

        while (sref)
        {
            if (sref->active)
            {
                ++n;

                SERVER* s = sref->server;
                uint64_t server_version = server_get_version(s);

                if (which == SERVICE_VERSION_MIN)
                {
                    if (server_version < v)
                    {
                        v = server_version;
                    }
                }
                else
                {
                    mxb_assert(which == SERVICE_VERSION_MAX);

                    if (server_version > v)
                    {
                        v = server_version;
                    }
                }
            }

            sref = sref->next;
        }

        if (n == 0)
        {
            v = 0;
        }

        version = v;
    }

    return version;
}

bool service_thread_init()
{
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        if (service->capabilities & ACAP_TYPE_ASYNC)
        {
            service_refresh_users(service);
        }
    }

    return true;
}

bool Service::is_basic_parameter(const std::string& name)
{
    static const std::set<std::string> names =
    {
        CN_AUTH_ALL_SERVERS,
        CN_CONNECTION_TIMEOUT,
        CN_ENABLE_ROOT_USER,
        CN_LOCALHOST_MATCH_WILDCARD_HOST,
        CN_LOG_AUTH_WARNINGS,
        CN_MAX_CONNECTIONS,
        CN_MAX_RETRY_INTERVAL,
        CN_PASSWORD,
        CN_RETRY_ON_FAILURE,
        CN_STRIP_DB_ESC,
        CN_USER,
        CN_VERSION_STRING,
        CN_WEIGHTBY,
        CN_FILTERS,
        CN_RETAIN_LAST_STATEMENTS
    };

    return names.find(name) != names.end();
}

void Service::update_basic_parameter(const std::string& key, const std::string& value)
{
    if (key == CN_USER)
    {
        m_user = value;
        snprintf(user, sizeof(user), "%s", value.c_str());
    }
    else if (key == CN_PASSWORD)
    {
        m_password = value;
        snprintf(password, sizeof(password), "%s", value.c_str());
    }
    else if (key == CN_ENABLE_ROOT_USER)
    {
        enable_root = config_truth_value(value.c_str());
    }
    else if (key == CN_MAX_RETRY_INTERVAL)
    {
        max_retry_interval = std::stoi(value);
        mxb_assert(max_retry_interval > 0);
    }
    else if (key == CN_MAX_CONNECTIONS)
    {
        max_connections = std::stoi(value);
        mxb_assert(max_connections > 0);
    }
    else if (key == CN_CONNECTION_TIMEOUT)
    {
        if ((conn_idle_timeout = std::stoi(value)))
        {
            dcb_enable_session_timeouts();
        }

        mxb_assert(conn_idle_timeout >= 0);
    }
    else if (key == CN_AUTH_ALL_SERVERS)
    {
        users_from_all = config_truth_value(value.c_str());
    }
    else if (key == CN_STRIP_DB_ESC)
    {
        strip_db_esc = config_truth_value(value.c_str());
    }
    else if (key == CN_LOCALHOST_MATCH_WILDCARD_HOST)
    {
        localhost_match_wildcard_host = config_truth_value(value.c_str());
    }
    else if (key == CN_VERSION_STRING)
    {
        m_version_string = value;
        snprintf(version_string, sizeof(version_string), "%s", value.c_str());
    }
    else if (key == CN_WEIGHTBY)
    {
        m_weightby = value;
        snprintf(weightby, sizeof(weightby), "%s", value.c_str());
    }
    else if (key == CN_LOG_AUTH_WARNINGS)
    {
        log_auth_warnings = config_truth_value(value.c_str());
    }
    else if (key == CN_RETRY_ON_FAILURE)
    {
        retry_start = config_truth_value(value.c_str());
    }
    else if (key == CN_RETAIN_LAST_STATEMENTS)
    {
        retain_last_statements = std::stoi(value);
    }
}
