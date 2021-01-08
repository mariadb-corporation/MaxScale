/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
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
#include <maxbase/alloc.h>
#include <maxbase/log.hh>
#include <maxscale/dcb.hh>
#include <maxscale/paths.hh>
#include <maxscale/poll.hh>
#include <maxscale/protocol.hh>
#include <maxscale/router.hh>
#include <maxscale/server.hh>
#include <maxscale/session.hh>
#include <maxscale/users.hh>
#include <maxscale/utils.h>
#include <maxscale/utils.hh>
#include <maxscale/version.h>
#include <maxscale/json_api.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/housekeeper.h>
#include <fstream>
#include <maxscale/modutil.hh>

#include "internal/config.hh"
#include "internal/filter.hh"
#include "internal/listener.hh"
#include "internal/modules.hh"
#include "internal/service.hh"
#include "internal/maxscale.hh"
#include "internal/servermanager.hh"
#include "internal/monitor.hh"

/** This define is needed in CentOS 6 systems */
#if !defined (UINT64_MAX)
#define UINT64_MAX (18446744073709551615UL)
#endif

using std::string;
using std::set;
using namespace maxscale;
using LockGuard = std::lock_guard<std::mutex>;
using UniqueLock = std::unique_lock<std::mutex>;

namespace
{
struct ThisUnit
{
    std::mutex            lock;
    std::vector<Service*> services;
} this_unit;

const char CN_AUTH_ALL_SERVERS[] = "auth_all_servers";
const char CN_LOCALHOST_MATCH_WILDCARD_HOST[] = "localhost_match_wildcard_host";
const char CN_LOG_AUTH_WARNINGS[] = "log_auth_warnings";
const char CN_MAX_CONNECTIONS[] = "max_connections";
const char CN_ROUTER_DIAGNOSTICS[] = "router_diagnostics";
const char CN_ROUTER_OPTIONS[] = "router_options";
const char CN_SESSION_TRACK_TRX_STATE[] = "session_track_trx_state";
const char CN_STRIP_DB_ESC[] = "strip_db_esc";
}

Service* Service::create(const char* name, const char* router, mxs::ConfigParameters* params)
{
    MXS_ROUTER_OBJECT* router_api = (MXS_ROUTER_OBJECT*)load_module(router, MODULE_ROUTER);

    if (router_api == NULL)
    {
        MXS_ERROR("Unable to load router module '%s'", router);
        return NULL;
    }

    // TODO: Think of a cleaner way to do this, e.g. reference.
    mxs::ConfigParameters empty;
    if (!params)
    {
        params = &empty;
    }

    Service* service = new(std::nothrow) Service(name, router, params);

    if (service == nullptr)
    {
        MXS_OOM();
        return NULL;
    }

    service->router_instance = router_api->createInstance(service, params);

    if (service->router_instance == NULL)
    {
        MXS_ERROR("%s: Failed to create router instance. Service not started.", service->name());
        service->state = State::FAILED;
        delete service;
        return NULL;
    }

    if (router_api->getCapabilities)
    {
        service->m_capabilities |= router_api->getCapabilities(service->router_instance);
    }

    LockGuard guard(this_unit.lock);
    this_unit.services.push_back(service);

    return service;
}

static std::string get_version_string(mxs::ConfigParameters* params)
{
    std::string version_string = params->get_string(CN_VERSION_STRING);

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

void service_add_server(Monitor* pMonitor, SERVER* pServer)
{
    LockGuard guard(this_unit.lock);

    for (Service* pService : this_unit.services)
    {
        if (pService->cluster() == pMonitor)
        {
            pService->add_target(pServer);
        }
    }
}

void service_remove_server(Monitor* pMonitor, SERVER* pServer)
{
    LockGuard guard(this_unit.lock);

    for (Service* pService : this_unit.services)
    {
        if (pService->cluster() == pMonitor)
        {
            pService->remove_target(pServer);
        }
    }
}

uint64_t Service::status() const
{
    uint64_t status = 0;

    for (auto a : m_data->servers)
    {
        if (a->is_master())
        {
            // Found master, stop searching
            status = SERVER_RUNNING | SERVER_MASTER;
            break;
        }

        if (a->is_running())
        {
            status |= SERVER_RUNNING;
        }

        if (a->is_slave())
        {
            status |= SERVER_SLAVE;
        }
    }

    return status;
}

Service::Config::Config(mxs::ConfigParameters* params)
    : user(params->get_string(CN_USER))
    , password(params->get_string(CN_PASSWORD))
    , version_string(get_version_string(params))
    , max_connections(params->get_integer(CN_MAX_CONNECTIONS))
    , enable_root(params->get_bool(CN_ENABLE_ROOT_USER))
    , users_from_all(params->get_bool(CN_AUTH_ALL_SERVERS))
    , log_auth_warnings(params->get_bool(CN_LOG_AUTH_WARNINGS))
    , session_track_trx_state(params->get_bool(CN_SESSION_TRACK_TRX_STATE))
    , conn_idle_timeout(params->get_duration<std::chrono::seconds>(CN_CONNECTION_TIMEOUT).count())
    , net_write_timeout(params->get_duration<std::chrono::seconds>(CN_NET_WRITE_TIMEOUT).count())
    , retain_last_statements(params->get_integer(CN_RETAIN_LAST_STATEMENTS))
    , connection_keepalive(params->get_duration<std::chrono::seconds>(CN_CONNECTION_KEEPALIVE).count())
    , strip_db_esc(params->get_bool(CN_STRIP_DB_ESC))
    , rank(params->get_enum(CN_RANK, rank_values))
{
}

Service::Service(const std::string& name,
                 const std::string& router_name,
                 mxs::ConfigParameters* params)
    : SERVICE(name, router_name)
    , m_config(params)
    , m_params(*params)
{
    const MXS_MODULE* module = get_module(router_name.c_str(), MODULE_ROUTER);
    mxb_assert(module);
    mxb_assert(load_module(router_name.c_str(), MODULE_ROUTER) == module->module_object);

    router = (MXS_ROUTER_OBJECT*)module->module_object;
    m_capabilities = module->module_capabilities;

    if (m_config->connection_keepalive)
    {
        // The connection keepalive relies on knowing when a connection is logically idle
        m_capabilities |= RCAP_TYPE_REQUEST_TRACKING;
    }
}

Service::~Service()
{
    mxb_assert((m_refcount == 0 && !active()) || maxscale_teardown_in_progress() || state == State::FAILED);

    if (router && router_instance && router->destroyInstance)
    {
        router->destroyInstance(router_instance);
    }

    auto manager = user_account_manager();
    if (manager)
    {
        manager->stop();
    }

    if (state != State::FAILED)
    {
        LockGuard guard(this_unit.lock);
        auto it = std::remove(this_unit.services.begin(), this_unit.services.end(), this);
        mxb_assert(it != this_unit.services.end());
        this_unit.services.erase(it);
        MXS_INFO("Destroying '%s'", name());
    }
}

// static
void Service::destroy(Service* service)
{
    mxb_assert(service->active());
    mxb_assert(mxs::MainWorker::is_main_worker());
    service->m_active = false;
    service->decref();
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

            if (listener->listen())
            {
                ++listeners;
            }
            else
            {
                return 0;
            }
        }

        if (service->state == SERVICE::State::FAILED)
        {
            listeners = 0;
        }
        else if (listeners)
        {
            service->state = SERVICE::State::STARTED;
            service->started = time(0);

            if (service->get_children().empty())
            {
                MXS_WARNING("Service '%s' has a listener but no servers", service->name());
            }
        }
    }
    else
    {
        MXS_WARNING("Service '%s' has no listeners defined.", service->name());
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
    int listeners = 0;

    if (!mxs::Config::get().config_check)
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

bool service_launch_all()
{
    int n = 0, i;
    bool ok = true;
    int num_svc = this_unit.services.size();

    if (num_svc > 0)
    {
        MXS_NOTICE("Starting a total of %d services...", num_svc);
    }
    else
    {
        MXS_NOTICE("No services defined in any of the configuration files");
    }

    int curr_svc = 1;
    for (Service* service : this_unit.services)
    {
        n += (i = serviceInitialize(service));
        MXS_NOTICE("Service '%s' started (%d/%d)", service->name(), curr_svc++, num_svc);

        if (i == 0)
        {
            MXS_ERROR("Failed to start service '%s'.", service->name());
            ok = false;
        }

        if (maxscale_is_shutting_down())
        {
            break;
        }
    }

    return ok;
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

        service->state = SERVICE::State::STOPPED;
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

        service->state = SERVICE::State::STARTED;
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

std::shared_ptr<Listener> service_find_listener(Service* service,
                                                const std::string& socket,
                                                const std::string& address,
                                                unsigned short port)
{
    std::shared_ptr<Listener> rval;
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

bool Service::can_be_destroyed() const
{
    const auto& data = *m_data;
    return listener_find_by_service(this).empty() && data.targets.empty() && data.filters.empty();
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
    *user = service->config()->user.c_str();
    *auth = service->config()->password.c_str();
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
            MXS_ERROR("Unable to find filter '%s' for service '%s'", f.c_str(), name());
            rval = false;
        }
    }

    if (rval)
    {
        m_data->filters = flist;
        m_data.assign(*m_data);
        m_capabilities |= my_capabilities;
    }

    return rval;
}

const Service::FilterList& Service::get_filters() const
{
    return m_data->filters;
}

void Service::remove_filter(SFilterDef filter)
{
    std::vector<std::string> new_filters;

    for (const auto& f : get_filters())
    {
        if (f != filter)
        {
            new_filters.push_back(f->name);
        }
    }

    set_filters(new_filters);
}

// static
Service* Service::find(const std::string& name)
{
    LockGuard guard(this_unit.lock);

    for (Service* s : this_unit.services)
    {
        if (s->name() == name && s->active())
        {
            return s;
        }
    }

    return nullptr;
}

std::vector<Service*> service_uses_monitor(mxs::Monitor* monitor)
{
    std::vector<Service*> rval;
    LockGuard guard(this_unit.lock);

    for (Service* s : this_unit.services)
    {
        if (s->cluster() == monitor)
        {
            rval.push_back(s);
        }
    }

    return rval;
}

/**
 * Return a named service
 *
 * @param servname      The name of the service to find
 * @return The service or NULL if not found
 */
SERVICE* service_find(const char* servname)
{
    return Service::find(servname);
}

void service_destroy_instances(void)
{
    // The global list is modified by service_free so we need a copy of it
    std::vector<Service*> my_services = this_unit.services;

    for (Service* s : my_services)
    {
        delete s;
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
        rval += service->stats().n_current;
    }

    return rval;
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
            MXS_ERROR("Service '%s' has no listeners.", service->name());
            rval = false;
        }
    }

    return rval;
}

std::vector<Service*> service_server_in_use(const SERVER* server)
{
    std::vector<Service*> rval;
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        LockGuard guard(service->lock);
        auto targets = service->get_children();

        if (std::find(targets.begin(), targets.end(), server) != targets.end())
        {
            rval.push_back(service);
        }
    }

    return rval;
}

std::vector<Service*> service_filter_in_use(const SFilterDef& filter)
{
    std::vector<Service*> rval;
    mxb_assert(filter);
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        for (const auto& f : service->get_filters())
        {
            if (filter == f)
            {
                rval.push_back(service);
                break;
            }
        }
    }

    return rval;
}

/**
 * Creates a service configuration at the location pointed by @c filename
 *
 * @param service Service to serialize into a configuration
 * @param filename Filename where configuration is written
 * @return True on success, false on error
 */
std::ostream& Service::persist(std::ostream& os) const
{
    const MXS_MODULE* mod = get_module(router_name(), NULL);
    mxb_assert(mod);

    mxs::ConfigParameters params_to_print = m_params;
    // The next text-mode parameter may not be up-to-date, print them manually. TODO: Fix
    params_to_print.remove(CN_FILTERS);
    params_to_print.remove(CN_SERVERS);
    params_to_print.remove(CN_TARGETS);
    params_to_print.remove(CN_CLUSTER);

    os << generate_config_string(name(), params_to_print, common_service_params(), mod->parameters);

    const auto& data = *m_data;

    std::vector<const char*> names;

    if (!data.filters.empty())
    {
        for (const auto& f : data.filters)
        {
            names.push_back(f->name.c_str());
        }

        os << CN_FILTERS << "=" << mxb::join(names, "|") << '\n';
        names.clear();
    }

    if (m_monitor)
    {
        os << CN_CLUSTER << "=" << m_monitor->name() << '\n';
    }
    else if (!data.targets.empty())
    {
        for (const auto& s : data.targets)
        {
            names.push_back(s->name());
        }

        os << CN_TARGETS << "=" << mxb::join(names, ",") << '\n';
        names.clear();
    }

    return os;
}

bool service_port_is_used(int port)
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

bool service_socket_is_used(const std::string& socket_path)
{
    bool rval = false;
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        for (const auto& listener : listener_find_by_service(service))
        {
            if (listener->address() == socket_path)
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

static const char* service_state_to_string(SERVICE::State state)
{
    switch (state)
    {
    case SERVICE::State::STARTED:
        return "Started";

    case SERVICE::State::STOPPED:
        return "Stopped";

    case SERVICE::State::FAILED:
        return "Failed";

    case SERVICE::State::ALLOC:
        return "Allocated";

    default:
        mxb_assert(false);
        return "Unknown";
    }
}

json_t* service_parameters_to_json(const SERVICE* service)
{
    json_t* rval = json_object();

    const MXS_MODULE* mod = get_module(service->router_name(), MODULE_ROUTER);
    config_add_module_params_json(&service->params(),
                                  {CN_TYPE, CN_ROUTER, CN_SERVERS, CN_FILTERS},
                                  common_service_params(),
                                  mod->parameters,
                                  rval);

    return rval;
}

static json_t* service_all_listeners_json_data(const char* host, const SERVICE* service)
{
    json_t* arr = json_array();

    for (const auto& listener : listener_find_by_service(service))
    {
        json_array_append_new(arr, listener->to_json(host));
    }

    return arr;
}

static json_t* service_listener_json_data(const char* host, const SERVICE* service, const char* name)
{
    auto listener = listener_find(name);

    if (listener && listener->service() == service)
    {
        return listener->to_json(host);
    }

    return NULL;
}

json_t* service_attributes(const char* host, const SERVICE* service)
{
    json_t* attr = json_object();

    json_object_set_new(attr, CN_ROUTER, json_string(service->router_name()));
    json_object_set_new(attr, CN_STATE, json_string(service_state_to_string(service->state)));

    if (service->router && service->router_instance && service->router->diagnostics)
    {
        json_t* diag = service->router->diagnostics(service->router_instance);

        if (diag)
        {
            json_object_set_new(attr, CN_ROUTER_DIAGNOSTICS, diag);
        }
    }

    struct tm result;
    char timebuf[30];

    asctime_r(localtime_r(&service->started, &result), timebuf);
    mxb::trim(timebuf);

    json_object_set_new(attr, "started", json_string(timebuf));
    json_object_set_new(attr, "total_connections", json_integer(service->stats().n_connections));
    json_object_set_new(attr, "connections", json_integer(service->stats().n_current));

    // The statistics for servers and services are located in different places in older versions. Newer
    // versions always have them in the statistics object of the attributes but they are also duplicated in
    // the service attributes for backwards compatibility.
    json_object_set_new(attr, "statistics", service->stats().to_json());

    /** Add service parameters and listeners */
    json_object_set_new(attr, CN_PARAMETERS, service_parameters_to_json(service));
    json_object_set_new(attr, CN_LISTENERS, service_all_listeners_json_data(host, service));

    return attr;
}

json_t* Service::json_relationships(const char* host) const
{
    /** Store relationships to other objects */
    json_t* rel = json_object();
    const auto& data = *m_data;
    std::string self = std::string(MXS_JSON_API_SERVICES) + name() + "/relationships/";

    if (!data.filters.empty())
    {
        json_t* filters = mxs_json_relationship(host, self + "filters", MXS_JSON_API_FILTERS);

        for (const auto& f : data.filters)
        {
            mxs_json_add_relation(filters, f->name.c_str(), CN_FILTERS);
        }

        json_object_set_new(rel, CN_FILTERS, filters);
    }

    if (m_monitor)
    {
        json_t* monitor = mxs_json_relationship(host, self + "monitors", MXS_JSON_API_MONITORS);
        mxs_json_add_relation(monitor, m_monitor->name(), CN_MONITORS);
        json_object_set_new(rel, CN_MONITORS, monitor);
    }
    else if (!data.targets.empty())
    {
        json_t* servers = mxs_json_relationship(host, self + "servers", MXS_JSON_API_SERVERS);
        json_t* services = mxs_json_relationship(host, self + "services", MXS_JSON_API_SERVICES);

        for (const auto& s : data.targets)
        {
            if (ServerManager::find_by_unique_name(s->name()))
            {
                mxs_json_add_relation(servers, s->name(), CN_SERVERS);
            }
            else
            {
                mxs_json_add_relation(services, s->name(), CN_SERVICES);
            }
        }

        json_object_set_new(rel, CN_SERVERS, servers);
        json_object_set_new(rel, CN_SERVICES, services);
    }

    auto listeners = listener_find_by_service(this);

    if (!listeners.empty())
    {
        json_t* l = mxs_json_relationship(host, self + "listeners", MXS_JSON_API_LISTENERS);

        for (const auto& a : listeners)
        {
            mxs_json_add_relation(l, a->name(), CN_LISTENERS);
        }

        json_object_set_new(rel, CN_LISTENERS, l);
    }

    return rel;
}

json_t* service_json_data(const SERVICE* svc, const char* host)
{
    const Service* service = static_cast<const Service*>(svc);
    json_t* rval = json_object();
    LockGuard guard(service->lock);

    json_object_set_new(rval, CN_ID, json_string(service->name()));
    json_object_set_new(rval, CN_TYPE, json_string(CN_SERVICES));
    json_object_set_new(rval, CN_ATTRIBUTES, service_attributes(host, service));
    json_object_set_new(rval, CN_RELATIONSHIPS, service->json_relationships(host));
    json_object_set_new(rval, CN_LINKS, mxs_json_self_link(host, CN_SERVICES, service->name()));

    return rval;
}

json_t* service_to_json(const Service* service, const char* host)
{
    string self = MXS_JSON_API_SERVICES;
    self += service->name();
    return mxs_json_resource(host, self.c_str(), service_json_data(service, host));
}

json_t* service_listener_list_to_json(const Service* service, const char* host)
{
    /** This needs to be done here as the listeners are sort of sub-resources
     * of the service. */
    string self = MXS_JSON_API_SERVICES;
    self += service->name();
    self += "/listeners";

    return mxs_json_resource(host, self.c_str(), service_all_listeners_json_data(host, service));
}

json_t* service_listener_to_json(const Service* service, const char* name, const char* host)
{
    /** This needs to be done here as the listeners are sort of sub-resources
     * of the service. */
    string self = MXS_JSON_API_SERVICES;
    self += service->name();
    self += "/listeners/";
    self += name;

    return mxs_json_resource(host, self.c_str(), service_listener_json_data(host, service, name));
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

json_t* service_relations_to_filter(const SFilterDef& filter, const std::string& host,
                                    const std::string& self)
{
    json_t* rel = nullptr;
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        for (const auto& f : service->get_filters())
        {
            if (f == filter)
            {
                if (!rel)
                {
                    rel = mxs_json_relationship(host, self, MXS_JSON_API_SERVICES);
                }
                mxs_json_add_relation(rel, service->name(), CN_SERVICES);
            }
        }
    }

    return rel;
}

json_t* service_relations_to_monitor(const mxs::Monitor* monitor, const std::string& host,
                                     const std::string& self)
{
    json_t* rel = nullptr;
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        if (service->cluster() == monitor)
        {
            if (!rel)
            {
                rel = mxs_json_relationship(host, self, MXS_JSON_API_SERVICES);
            }

            mxs_json_add_relation(rel, service->name(), CN_SERVICES);
        }
    }

    return rel;
}

json_t* service_relations_to_server(const SERVER* server, const std::string& host, const std::string& self)
{
    std::vector<std::string> names;
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        LockGuard guard(service->lock);
        auto targets = service->get_children();

        if (std::find(targets.begin(), targets.end(), server) != targets.end())
        {
            names.push_back(service->name());
        }
    }

    std::sort(names.begin(), names.end());

    json_t* rel = NULL;

    if (!names.empty())
    {
        rel = mxs_json_relationship(host, self, MXS_JSON_API_SERVICES);

        for (const auto& a : names)
        {
            mxs_json_add_relation(rel, a.c_str(), CN_SERVICES);
        }
    }

    return rel;
}

uint64_t service_get_version(const SERVICE* svc, service_version_which_t which)
{
    return static_cast<const Service*>(svc)->get_version(which);
}

bool Service::is_basic_parameter(const std::string& name)
{
    static const std::set<std::string> names =
    {
        CN_AUTH_ALL_SERVERS,
        CN_CONNECTION_TIMEOUT,
        CN_NET_WRITE_TIMEOUT,
        CN_ENABLE_ROOT_USER,
        CN_LOCALHOST_MATCH_WILDCARD_HOST,
        CN_LOG_AUTH_WARNINGS,
        CN_MAX_CONNECTIONS,
        CN_PASSWORD,
        CN_STRIP_DB_ESC,
        CN_USER,
        CN_VERSION_STRING,
        CN_FILTERS,
        CN_RETAIN_LAST_STATEMENTS,
        CN_CONNECTION_KEEPALIVE,
        CN_RANK
    };

    return names.find(name) != names.end();
}

void Service::update_basic_parameters(const mxs::ConfigParameters& params)
{
    m_params.set_multiple(params);
    m_config.assign(Config(&m_params));

    const auto& config = *m_config;
    if (config.connection_keepalive)
    {
        m_capabilities |= RCAP_TYPE_REQUEST_TRACKING;
    }

    // If the parameter affects the user account manager, update its settings.
    if (m_usermanager)
    {
        m_usermanager->set_credentials(config.user, config.password);
        m_usermanager->set_union_over_backends(config.users_from_all);
        m_usermanager->set_strip_db_esc(config.strip_db_esc);
    }
}

void Service::update_basic_parameter(const std::string& key, const std::string& value)
{
    m_params.set(key, value);
    update_basic_parameters(m_params);
}

const MXS_MODULE_PARAM* common_service_params()
{
    static const MXS_MODULE_PARAM config_service_params[] =
    {
        {CN_TYPE,                 MXS_MODULE_PARAM_STRING,   CN_SERVICE, MXS_MODULE_OPT_REQUIRED  },
        {CN_ROUTER,               MXS_MODULE_PARAM_STRING,   NULL,       MXS_MODULE_OPT_REQUIRED  },
        {CN_ROUTER_OPTIONS,       MXS_MODULE_PARAM_STRING},
        {CN_SERVERS,              MXS_MODULE_PARAM_STRING},
        {CN_TARGETS,              MXS_MODULE_PARAM_STRING},
        {CN_USER,                 MXS_MODULE_PARAM_STRING,   NULL,       MXS_MODULE_OPT_REQUIRED  },
        {CN_PASSWORD,             MXS_MODULE_PARAM_PASSWORD, NULL,       MXS_MODULE_OPT_REQUIRED  },
        {CN_ENABLE_ROOT_USER,     MXS_MODULE_PARAM_BOOL,     "false"},
        {CN_MAX_CONNECTIONS,      MXS_MODULE_PARAM_COUNT,    "0"},
        {CN_CONNECTION_TIMEOUT,   MXS_MODULE_PARAM_DURATION, "0",        MXS_MODULE_OPT_DURATION_S},
        {CN_NET_WRITE_TIMEOUT,    MXS_MODULE_PARAM_DURATION, "0",        MXS_MODULE_OPT_DURATION_S},
        {CN_AUTH_ALL_SERVERS,     MXS_MODULE_PARAM_BOOL,     "false"},
        {CN_STRIP_DB_ESC,         MXS_MODULE_PARAM_BOOL,     "true"},
        {
            CN_LOCALHOST_MATCH_WILDCARD_HOST, MXS_MODULE_PARAM_BOOL, "true", MXS_MODULE_OPT_DEPRECATED
        },
        {CN_VERSION_STRING,       MXS_MODULE_PARAM_STRING},
        {CN_FILTERS,              MXS_MODULE_PARAM_STRING},
        {CN_LOG_AUTH_WARNINGS,    MXS_MODULE_PARAM_BOOL,     "true"},
        {
            CN_SESSION_TRACK_TRX_STATE, MXS_MODULE_PARAM_BOOL, "false"
        },
        {
            CN_RETAIN_LAST_STATEMENTS, MXS_MODULE_PARAM_INT, "-1"
        },
        {CN_SESSION_TRACE,        MXS_MODULE_PARAM_BOOL,     "false"},
        {CN_CLUSTER,              MXS_MODULE_PARAM_STRING},
        {
            CN_RANK, MXS_MODULE_PARAM_ENUM, DEFAULT_RANK, MXS_MODULE_OPT_ENUM_UNIQUE, rank_values
        },
        {CN_CONNECTION_KEEPALIVE, MXS_MODULE_PARAM_DURATION, "300s",     MXS_MODULE_OPT_DURATION_S},
        {NULL}
    };
    return config_service_params;
}

ServiceEndpoint::ServiceEndpoint(MXS_SESSION* session, Service* service, mxs::Component* up)
    : m_up(up)
    , m_session(session)
    , m_service(service)
{
    service->incref();
}

ServiceEndpoint::~ServiceEndpoint()
{
    if (is_open())
    {
        close();
    }

    m_service->decref();
}

// static
int32_t ServiceEndpoint::upstream_function(MXS_FILTER* instance,
                                           MXS_FILTER_SESSION* session,
                                           GWBUF* buffer,
                                           const mxs::ReplyRoute& down,
                                           const mxs::Reply& reply)
{
    ServiceEndpoint* self = reinterpret_cast<ServiceEndpoint*>(session);
    return self->send_upstream(buffer, down, reply);
}

int32_t ServiceEndpoint::send_upstream(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxs::ReplyRoute& d = const_cast<mxs::ReplyRoute&>(down);
    d.push_back(this);
    return m_up->clientReply(buffer, d, reply);
}

void ServiceEndpoint::set_endpoints(std::vector<std::unique_ptr<mxs::Endpoint>> down)
{
    m_down = std::move(down);
}

mxs::Target* ServiceEndpoint::target() const
{
    return m_service;
}

bool ServiceEndpoint::connect()
{
    mxb::LogScope scope(m_service->name());
    std::vector<mxs::Endpoint*> endpoints;
    endpoints.reserve(m_down.size());
    std::transform(m_down.begin(), m_down.end(), std::back_inserter(endpoints),
                   std::mem_fn(&std::unique_ptr<mxs::Endpoint>::get));

    m_router_session = m_service->router->newSession(m_service->router_instance, m_session,
                                                     &m_tail, endpoints);

    if (!m_router_session)
    {
        MXS_ERROR("Failed to create new router session for service '%s'. "
                  "See previous errors for more details.", m_service->name());
        return false;
    }

    m_head.instance = (MXS_FILTER*)m_service->router_instance;
    m_head.session = (MXS_FILTER_SESSION*)m_router_session;
    m_head.routeQuery = (DOWNSTREAMFUNC)m_service->router->routeQuery;

    m_tail.instance = (MXS_FILTER*)this;
    m_tail.session = (MXS_FILTER_SESSION*)this;
    m_tail.clientReply = ServiceEndpoint::upstream_function;

    for (const auto& a : m_service->get_filters())
    {
        m_filters.emplace_back(a);
    }

    for (auto it = m_filters.begin(); it != m_filters.end(); ++it)
    {
        auto& f = *it;
        f.session = f.filter->obj->newSession(f.instance, m_session, m_service, &f.down, &f.up);

        if (!f.session)
        {
            MXS_ERROR("Failed to create filter session for '%s'", f.filter->name.c_str());

            for (auto d = m_filters.begin(); d != it; ++d)
            {
                mxb_assert(d->session);
                d->filter->obj->closeSession(d->instance, d->session);
                d->filter->obj->freeSession(d->instance, d->session);
            }

            m_filters.clear();
            return false;
        }
    }

    // The head of the chain currently points at the router
    mxs::Downstream chain_head = m_head;

    for (auto it = m_filters.rbegin(); it != m_filters.rend(); it++)
    {
        it->down = chain_head;
        chain_head.instance = it->instance;
        chain_head.session = it->session;
        chain_head.routeQuery = it->filter->obj->routeQuery;
    }

    m_head = chain_head;

    // The tail is the upstream component of the service (the client DCB)
    mxs::Upstream chain_tail = m_tail;

    for (auto it = m_filters.begin(); it != m_filters.end(); it++)
    {
        it->up = chain_tail;
        chain_tail.instance = it->instance;
        chain_tail.session = it->session;
        chain_tail.clientReply = it->filter->obj->clientReply;
    }

    m_tail = chain_tail;

    // The endpoint is now "connected"
    m_open = true;

    m_service->stats().add_connection();

    return true;
}

void ServiceEndpoint::close()
{
    mxb::LogScope scope(m_service->name());
    mxb_assert(m_open);
    m_service->router->closeSession(m_service->router_instance, m_router_session);

    for (auto& a : m_filters)
    {
        a.filter->obj->closeSession(a.instance, a.session);
    }

    m_service->router->freeSession(m_service->router_instance, m_router_session);

    for (auto& a : m_filters)
    {
        a.filter->obj->freeSession(a.instance, a.session);
    }

    // Propagate the close to the downstream endpoints
    for (auto& a : m_down)
    {
        if (a->is_open())
        {
            a->close();
        }
    }

    m_open = false;

    m_service->stats().remove_connection();
}

bool ServiceEndpoint::is_open() const
{
    return m_open;
}

int32_t ServiceEndpoint::routeQuery(GWBUF* buffer)
{
    mxb::LogScope scope(m_service->name());
    mxb_assert(m_open);
    return m_head.routeQuery(m_head.instance, m_head.session, buffer);
}

int32_t ServiceEndpoint::clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb::LogScope scope(m_service->name());
    mxb_assert(m_open);
    m_service->router->clientReply(m_service->router_instance, m_router_session, buffer, down, reply);

    // TODO: Add a proper return value to router's clientReply
    return 1;
}

bool ServiceEndpoint::handleError(mxs::ErrorType type, GWBUF* error,
                                  mxs::Endpoint* down, const mxs::Reply& reply)
{
    mxb::LogScope scope(m_service->name());
    mxb_assert(m_open);
    bool ok = m_service->router->handleError(m_service->router_instance, m_router_session,
                                             type, error, down, reply);

    if (!ok)
    {
        ok = m_up->handleError(type, error, this, reply);
    }

    return ok;
}

std::unique_ptr<mxs::Endpoint> Service::get_connection(mxs::Component* up, MXS_SESSION* session)
{
    std::unique_ptr<ServiceEndpoint> my_connection(new(std::nothrow) ServiceEndpoint(session, this, up));

    if (my_connection)
    {
        std::vector<std::unique_ptr<mxs::Endpoint>> connections;
        connections.reserve(m_data->targets.size());

        for (auto a : m_data->targets)
        {
            connections.push_back(a->get_connection(my_connection.get(), session));
            mxb_assert(connections.back().get());
        }

        my_connection->set_endpoints(std::move(connections));
    }

    return std::unique_ptr<mxs::Endpoint>(my_connection.release());
}

namespace
{

// Returns all servers that are in the given list of targets
std::vector<SERVER*> get_servers(std::vector<mxs::Target*> targets)
{
    std::vector<SERVER*> rval;

    for (auto a : targets)
    {
        if (auto srv = ServerManager::find_by_unique_name(a->name()))
        {
            rval.push_back(srv);
        }
        else
        {
            auto servers = get_servers(a->get_children());
            rval.insert(rval.end(), servers.begin(), servers.end());
        }
    }

    std::sort(rval.begin(), rval.end());
    rval.erase(std::unique(rval.begin(), rval.end()), rval.end());

    return rval;
}

// Recursively get routing capabilities
uint64_t get_capabilities(std::vector<mxs::Target*> targets)
{
    uint64_t rval = 0;

    for (auto a : targets)
    {
        rval |= a->capabilities() | get_capabilities(a->get_children());
    }

    return rval;
}
}

// Returns minimum and maximum server versions from the list of servers
std::pair<uint64_t, uint64_t> Service::get_versions(const std::vector<SERVER*>& servers) const
{
    uint64_t v_max = 0;
    uint64_t v_min = 0;

    if (!servers.empty())
    {
        v_min = UINT64_MAX;

        for (auto s : servers)
        {
            auto srv_version = s->info().version_num().total;
            v_min = std::min(srv_version, v_min);
            v_max = std::max(srv_version, v_max);
        }
    }

    return {v_min, v_max};
}

void Service::targets_updated()
{
    auto& data = *m_data;

    // Now that we have the new set of targets, recalculate the servers that this service reaches as well as
    // the new routing capabilities.
    data.servers = get_servers(data.targets);
    data.target_capabilities = get_capabilities(data.targets);

    // Update the global value based on the local cached value. Since modifications to services are always
    // done on the same thread, there's no possibility of lost updates.
    mxb_assert(mxs::MainWorker::is_main_worker());
    m_data.assign(data);

    // Also update the servers queried by the user account manager.
    auto manager = user_account_manager();
    if (manager)
    {
        manager->set_backends(data.servers);
    }
}

void Service::remove_target(mxs::Target* target)
{
    auto& targets = m_data->targets;
    targets.erase(std::remove(targets.begin(), targets.end(), target), targets.end());
    targets_updated();
}

void Service::add_target(mxs::Target* target)
{
    m_data->targets.push_back(target);
    targets_updated();
}

int64_t Service::replication_lag() const
{
    int64_t lag = mxs::Target::RLAG_UNDEFINED;

    for (auto a : m_data->targets)
    {
        int64_t l = a->replication_lag();

        if (lag == mxs::Target::RLAG_UNDEFINED || l < lag)
        {
            lag = l;
        }
    }

    return lag;
}

uint64_t Service::gtid_pos(uint32_t domain) const
{
    uint64_t max_pos = 0;

    for (auto t : m_data->targets)
    {
        auto pos = t->gtid_pos(domain);

        if (pos > max_pos)
        {
            pos = max_pos;
        }
    }

    return max_pos;
}

int64_t Service::ping() const
{
    int64_t undef = mxs::Target::PING_UNDEFINED;
    auto rval = undef;
    for (auto a : m_data->targets)
    {
        auto p = a->ping();
        if (p != undef && (rval == undef || p < rval))
        {
            rval = p;
        }
    }
    return rval;
}

void Service::incref()
{
    m_refcount.fetch_add(1, std::memory_order_relaxed);
}

void Service::decref()
{
    if (m_refcount.fetch_add(-1, std::memory_order_acq_rel) == 1)
    {
        // Destroy the service in the main routing worker thread
        mxs::MainWorker::get()->execute(
            [this]() {
                delete this;
            }, mxs::RoutingWorker::EXECUTE_AUTO);
    }
}

UserAccountManager* Service::user_account_manager()
{
    return m_usermanager.get();
}

const mxs::UserAccountCache* Service::user_account_cache() const
{
    mxb_assert(mxs::RoutingWorker::get_current());
    return m_usercache->get();
}

/**
 * Set account manager. Must not be called more than once for the same service.
 *
 * @param user_manager The user account manager this service will use
 */
void Service::set_start_user_account_manager(SAccountManager user_manager)
{
    // Once the object is set, it can not change as this would indicate a change in service
    // backend protocol.
    mxb_assert(!m_usermanager);
    m_usermanager = std::move(user_manager);
    const auto& config = *m_config;
    m_usermanager->set_credentials(config.user, config.password);
    m_usermanager->set_backends(m_data->servers);
    m_usermanager->set_union_over_backends(config.users_from_all);
    m_usermanager->set_strip_db_esc(config.strip_db_esc);
    m_usermanager->set_service(this);

    // Message each routingworker to initialize their own user caches. Wait for completion so that
    // the admin thread and workers see the same object.
    mxb::Semaphore sem;
    auto init_cache = [this]() {
            *m_usercache = user_account_manager()->create_user_account_cache();
        };
    auto n_threads = mxs::RoutingWorker::broadcast(init_cache, &sem, mxb::Worker::EXECUTE_AUTO);
    sem.wait_n(n_threads);

    m_usermanager->start();
}

void Service::request_user_account_update()
{
    user_account_manager()->update_user_accounts();
}

void Service::sync_user_account_caches()
{
    // Message each routingworker to update their caches. Do not wait for operation to finish.
    auto update_cache = [this]() {
            auto& user_cache = *m_usercache;
            if (user_cache)
            {
                user_cache->update_from_master();
            }
            wakeup_sessions_waiting_userdata();
        };
    mxs::RoutingWorker::broadcast(update_cache, nullptr, mxb::Worker::EXECUTE_AUTO);
}

std::string SERVICE::version_string() const
{
    // User-defined version string, use it if available
    std::string rval = config()->version_string;

    if (rval.empty())
    {
        uint64_t smallest_found = UINT64_MAX;
        for (auto server : reachable_servers())
        {
            auto& info = server->info();
            auto version = info.version_num().total;
            if (version > 0 && version < smallest_found)
            {
                rval = info.version_string();
                smallest_found = version;
            }
        }

        if (!m_custom_version_suffix.empty() && !rval.empty())
        {
            rval.append(m_custom_version_suffix);
        }
    }

    return rval;
}

uint8_t SERVICE::charset() const
{
    uint8_t rval = 0;

    for (auto s : reachable_servers())
    {
        if (s->charset())
        {
            if (s->is_master())
            {
                // Master found, stop searching
                rval = s->charset();
                break;
            }
            else if (s->is_slave() || rval == 0)
            {
                // Slaves precede Running servers and server that are Down but whose charset is known
                rval = s->charset();
            }
        }
    }

    if (rval == 0)
    {
        rval = 0x08;    // The default charset: latin1
    }

    return rval;
}

const std::string& SERVICE::custom_version_suffix()
{
    return m_custom_version_suffix;
}

void SERVICE::set_custom_version_suffix(const string& custom_version_suffix)
{
    mxb_assert(m_custom_version_suffix.empty());    // Should only be set once.
    m_custom_version_suffix = custom_version_suffix;
}

void Service::wakeup_sessions_waiting_userdata()
{
    auto& sleeping_clients = *m_sleeping_clients;
    for (auto* sleeper : sleeping_clients)
    {
        sleeper->wakeup();
    }
    sleeping_clients.clear();
}

void Service::mark_for_wakeup(mxs::ClientConnection* session)
{
    MXB_AT_DEBUG(auto ret = ) m_sleeping_clients->insert(session);
    mxb_assert(ret.second);
}

void Service::unmark_for_wakeup(mxs::ClientConnection* session)
{
    // Should not assert here, as there may be some corner cases where the connection has just been removed
    // from the set but event was not processed before closing.
    m_sleeping_clients->erase(session);
}

bool Service::check_update_user_account_manager(mxs::ProtocolModule* protocol_module, const string& listener)
{
    // If the service does not yet have a user data manager, create one and set to service.
    // If one already exists, check that it's for the same protocol the current listener is using.
    bool rval = false;
    auto new_proto_name = protocol_module->name();
    auto listenerz = listener.c_str();

    if (m_usermanager)
    {
        // This name comparison needs to be done by querying the modules themselves since
        // the actual setting value has aliases.
        if (new_proto_name == m_usermanager->protocol_name())
        {
            rval = true;
        }
        else
        {
            // If ever multiple backend protocols need to be supported on the same service,
            // multiple usermanagers are also needed.
            MXB_ERROR("The protocol of listener '%s' ('%s') differs from the protocol in the target "
                      "service '%s' ('%s') when both protocols implement user account management. ",
                      listenerz, new_proto_name.c_str(),
                      name(), m_usermanager->protocol_name().c_str());
        }
    }
    else
    {
        auto new_user_manager = protocol_module->create_user_data_manager();
        if (new_user_manager)
        {
            set_start_user_account_manager(move(new_user_manager));
            rval = true;
        }
        else
        {
            MXB_ERROR("Failed to create an user account manager for protocol '%s' of listener '%s'.",
                      new_proto_name.c_str(), listenerz);
        }
    }
    return rval;
}

void Service::set_cluster(mxs::Monitor* monitor)
{
    m_monitor = monitor;
}

bool Service::change_cluster(mxs::Monitor* monitor)
{
    bool rval = false;

    if (m_monitor == nullptr && m_data->targets.empty())
    {
        for (auto a : monitor->servers())
        {
            m_data->targets.push_back(a->server);
        }

        targets_updated();
        m_monitor = monitor;
        rval = true;
    }

    return rval;
}

bool Service::remove_cluster(mxs::Monitor* monitor)
{
    bool rval = false;

    if (m_monitor == monitor)
    {
        m_data->targets.clear();
        targets_updated();
        m_monitor = nullptr;
        rval = true;
    }

    return rval;
}
