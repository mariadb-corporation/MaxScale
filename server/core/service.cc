/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file service.c  - A representation of a service within MaxScale
 */

#include "internal/service.hh"

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
#include <fstream>
#include <utility>

#include <maxbase/jansson.hh>
#include <maxbase/log.hh>
#include <maxscale/config2.hh>
#include <maxscale/dcb.hh>
#include <maxscale/http.hh>
#include <maxscale/json_api.hh>
#include <maxscale/listener.hh>
#include <maxscale/paths.hh>
#include <maxscale/protocol.hh>
#include <maxscale/router.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/server.hh>
#include <maxscale/session.hh>
#include <maxscale/users.hh>
#include <maxscale/utils.hh>
#include <maxscale/version.hh>

#include "internal/config.hh"
#include "internal/config_runtime.hh"
#include "internal/filter.hh"
#include "internal/maxscale.hh"
#include "internal/modules.hh"
#include "internal/monitormanager.hh"
#include "internal/servermanager.hh"

/** This define is needed in CentOS 6 systems */
#if !defined (UINT64_MAX)
#define UINT64_MAX (18446744073709551615UL)
#endif

using std::string;
using std::set;
using std::vector;
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
const char CN_IDLE_SESSION_POOL_TIME[] = "idle_session_pool_time";

namespace cfg = mxs::config;

class ServiceSpec : public cfg::Specification
{
public:
    using cfg::Specification::Specification;

private:
    template<class Params>
    bool do_post_validate(Params& params) const;

    bool post_validate(const cfg::Configuration* config,
                       const mxs::ConfigParameters& params,
                       const std::map<std::string, mxs::ConfigParameters>& nested_params) const override
    {
        return do_post_validate(params);
    }

    bool post_validate(const cfg::Configuration* config,
                       json_t* json,
                       const std::map<std::string, json_t*>& nested_params) const override
    {
        return do_post_validate(json);
    }
};

ServiceSpec s_spec(CN_SERVICES, cfg::Specification::ROUTER);

cfg::ParamString s_type(&s_spec, CN_TYPE, "The type of the object", CN_SERVICE);
cfg::ParamModule s_router(&s_spec, CN_ROUTER, "The router to use", mxs::ModuleType::ROUTER);

cfg::ParamStringList s_servers(
    &s_spec, "servers", "List of servers to use",
    ",", {}, cfg::Param::AT_RUNTIME);

cfg::ParamStringList s_targets(
    &s_spec, "targets", "List of targets to use",
    ",", {}, cfg::Param::AT_RUNTIME);

cfg::ParamString s_cluster(
    &s_spec, "cluster", "The cluster of servers to use",
    "", cfg::Param::AT_RUNTIME);

cfg::ParamStringList s_filters(
    &s_spec, "filters", "List of filters to use",
    "|", {}, cfg::Param::AT_RUNTIME);

cfg::ParamString s_user(
    &s_spec, "user", "Username used to retrieve database users",
    cfg::Param::AT_RUNTIME);

cfg::ParamPassword s_password(
    &s_spec, "password", "Password for the user used to retrieve database users",
    cfg::Param::AT_RUNTIME);

cfg::ParamBool s_enable_root_user(
    &s_spec, "enable_root_user", "Allow the root user to connect to this service",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamCount s_max_connections(
    &s_spec, "max_connections", "Maximum number of connections",
    0, cfg::Param::AT_RUNTIME);

cfg::ParamSeconds s_connection_timeout(
    &s_spec, "connection_timeout", "Connection idle timeout",
    std::chrono::seconds(0), cfg::Param::AT_RUNTIME);

cfg::server::DurationDependency<std::chrono::seconds> s_dep_connection_timeout(
    "wait_timeout", &s_connection_timeout, cfg::server::Dependency::MIN);

cfg::ParamSeconds s_net_write_timeout(
    &s_spec, "net_write_timeout", "Network write timeout",
    std::chrono::seconds(0), cfg::Param::AT_RUNTIME);

cfg::ParamBool s_auth_all_servers(
    &s_spec, "auth_all_servers", "Retrieve users from all backend servers instead of only one",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_strip_db_esc(
    &s_spec, "strip_db_esc", "Strip escape characters from database names",
    true, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_localhost_match_wildcard_host(
    &s_spec, "localhost_match_wildcard_host", "Match localhost to wildcard host",
    true, cfg::Param::AT_RUNTIME);

cfg::ParamString s_version_string(
    &s_spec, "version_string", "Custom version string to use",
    "", cfg::Param::AT_RUNTIME);

cfg::ParamBool s_log_auth_warnings(
    &s_spec, "log_auth_warnings", "Log a warning when client authentication fails",
    true, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_session_track_trx_state(
    &s_spec, "session_track_trx_state", "Track session state using server responses",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamInteger s_retain_last_statements(
    &s_spec, "retain_last_statements", "Number of statements kept in memory",
    -1, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_session_trace(
    &s_spec, "session_trace", "Enable session tracing for this service",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamEnum<int64_t> s_rank(
    &s_spec, CN_RANK, "Service rank",
    {
        {RANK_PRIMARY, "primary"},
        {RANK_SECONDARY, "secondary"}
    }, RANK_PRIMARY, cfg::Param::AT_RUNTIME);

cfg::ParamSeconds s_connection_keepalive(
    &s_spec, "connection_keepalive", "How often idle connections are pinged",
    std::chrono::seconds(300), cfg::Param::AT_RUNTIME);

cfg::server::DurationDependency<std::chrono::seconds> s_dep_connection_keepalive(
    "wait_timeout", &s_connection_keepalive, cfg::server::Dependency::MIN, 80);

cfg::ParamBool s_prune_sescmd_history(
    &s_spec, "prune_sescmd_history", "Prune old session command history if the limit is exceeded",
    true, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_disable_sescmd_history(
    &s_spec, "disable_sescmd_history", "Disable session command history",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamCount s_max_sescmd_history(
    &s_spec, "max_sescmd_history", "Session command history size",
    50, cfg::Param::AT_RUNTIME);

cfg::ParamMilliseconds s_idle_session_pool_time(
    &s_spec, "idle_session_pool_time", "Put connections into pool after session has been idle for this long",
    std::chrono::milliseconds(-1),
    cfg::ParamMilliseconds::DurationType::SIGNED, cfg::Param::AT_RUNTIME);

cfg::ParamSeconds s_multiplex_timeout(
    &s_spec, "multiplex_timeout", "How long a session can wait for a connection to become available",
    std::chrono::seconds(60), cfg::ParamSeconds::DurationType::UNSIGNED, cfg::Param::AT_RUNTIME);

cfg::ParamPath s_user_accounts_file(
    &s_spec, "user_accounts_file", "Load additional users from a file",
    cfg::ParamPath::Options::R | cfg::ParamPath::Options::F, "", cfg::Param::AT_STARTUP);

cfg::ParamEnum<UserAccountManager::UsersFileUsage> s_user_accounts_file_usage(
    &s_spec, "user_accounts_file_usage", "When and how the user accounts file is used",
    {
        {UserAccountManager::UsersFileUsage::ADD_WHEN_LOAD_OK, "add_when_load_ok"},
        {UserAccountManager::UsersFileUsage::FILE_ONLY_ALWAYS, "file_only_always"},
    }, UserAccountManager::UsersFileUsage::ADD_WHEN_LOAD_OK, cfg::Param::AT_STARTUP);

cfg::ParamBool s_log_debug(
    &s_spec, "log_debug", "Log debug messages for this service (debug builds only)",
    false, cfg::Param::Modifiable::AT_RUNTIME);

cfg::ParamBool s_log_info(
    &s_spec, "log_info", "Log info messages for this service",
    false, cfg::Param::Modifiable::AT_RUNTIME);

cfg::ParamBool s_log_notice(
    &s_spec, "log_notice", "Log notice messages for this service",
    false, cfg::Param::Modifiable::AT_RUNTIME);

cfg::ParamBool s_log_warning(
    &s_spec, "log_warning", "Log warning messages for this service",
    false, cfg::Param::Modifiable::AT_RUNTIME);

template<class Params>
bool ServiceSpec::do_post_validate(Params& params) const
{
    bool ok = true;
    auto servers = s_servers.get(params);
    auto targets = s_targets.get(params);
    std::string cluster = s_cluster.get(params);

    if (!servers.empty() + !targets.empty() + !cluster.empty() > 1)
    {
        MXB_ERROR("Only one '%s', '%s' or '%s' is allowed.", s_servers.name().c_str(),
                  s_targets.name().c_str(), s_cluster.name().c_str());
        ok = false;
    }
    else if (!servers.empty())
    {
        auto it = std::find_if_not(servers.begin(), servers.end(), ServerManager::find_by_unique_name);

        if (it != servers.end())
        {
            MXB_ERROR("'%s' is not a valid server", it->c_str());
            ok = false;
        }
    }
    else if (!targets.empty())
    {
        auto it = std::find_if_not(targets.begin(), targets.end(), mxs::Target::find);

        if (it != targets.end())
        {
            MXB_ERROR("'%s' is not a valid target", it->c_str());
            ok = false;
        }
    }
    else if (!cluster.empty())
    {
        if (!MonitorManager::find_monitor(cluster.c_str()))
        {
            MXB_ERROR("'%s' is not a valid cluster", cluster.c_str());
            ok = false;
        }
    }

    auto filters = s_filters.get(params);

    if (!filters.empty())
    {
        auto it = std::find_if_not(filters.begin(), filters.end(), filter_find);

        if (it != filters.end())
        {
            MXB_ERROR("'%s' is not a valid filter", it->c_str());
            ok = false;
        }
    }

    return ok;
}

std::set<std::string> merge_protocols(const std::set<std::string>& lhs, const std::set<std::string>& rhs)
{
    if (lhs.count(MXS_ANY_PROTOCOL))
    {
        return rhs;
    }
    else if (rhs.count(MXS_ANY_PROTOCOL))
    {
        return lhs;
    }

    std::set<std::string> intersection;
    std::set_intersection(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                          std::inserter(intersection, intersection.begin()));
    return intersection;
}

std::set<std::string> get_target_protocols(std::set<std::string>protocols,
                                           const std::vector<mxs::Target*>& targets)
{
    for (mxs::Target* t : targets)
    {
        protocols = merge_protocols(protocols, get_target_protocols(t->protocols(), t->get_children()));
    }

    return protocols;
}
}

// static
mxs::config::Specification* Service::specification()
{
    return &s_spec;
}

// static
template<class Params, class Unknown>
Service* Service::create(const std::string& name, Params params, Unknown unknown)
{
    if (!s_spec.validate(params, &unknown))
    {
        return nullptr;
    }

    auto module = s_router.get(params);
    const char* router = module->name;

    if (module->specification && !module->specification->validate(params))
    {
        return nullptr;
    }

    std::unique_ptr<Service> service(new Service(name, router));

    if (!service->m_config.configure(params, &unknown))
    {
        service->state = State::FAILED;
        return nullptr;
    }

    // TODO: Change the router API to use a reference
    MXS_ROUTER_API* router_api = (MXS_ROUTER_API*)module->module_object;
    service->m_router.reset(router_api->createInstance(service.get()));

    if (!service->m_router)
    {
        MXB_ERROR("%s: Failed to create router instance. Service not started.", service->name());
        service->state = State::FAILED;
        return nullptr;
    }

    auto servers = s_servers.get(params);
    auto targets = s_targets.get(params);
    auto cluster = s_cluster.get(params);
    auto filters = s_filters.get(params);

    // The call to set_filters also calculates the capabilities and the set of
    // supported protocols for this service.
    if (!service->set_filters(filters))
    {
        service->state = State::FAILED;
        return nullptr;
    }

    // The set of supported protocols is calculated in set_filters()
    mxb_assert(!service->m_protocols.empty());

    auto ours = service->protocols();

    // The values for the various target types and filters are only read from the configuration object when
    // the service is created. At runtime, the servers are added individually via add_target().
    if (!servers.empty())
    {
        for (const auto& a : servers)
        {
            Server* server = ServerManager::find_by_unique_name(a);
            mxb_assert(server);
            service->add_target(server);
        }
    }
    else if (!targets.empty())
    {
        for (const auto& a : targets)
        {
            if (auto s = ServerManager::find_by_unique_name(a))
            {
                service->add_target(s);
            }
            else if (auto s = Service::find(mxb::trimmed_copy(a)))
            {
                auto theirs = get_target_protocols(s->m_protocols, s->m_data->targets);
                auto combined = merge_protocols(ours, theirs);

                if (combined.empty())
                {
                    MXB_ERROR("Protocol mismatch: service '%s' supports '%s' but service '%s' requires '%s'.",
                              service->name(), mxb::join(ours).c_str(), s->name(), mxb::join(theirs).c_str());
                    service->state = State::FAILED;
                    return nullptr;
                }
                else
                {
                    service->add_target(s);
                    ours = std::move(combined);
                }
            }
            else
            {
                mxb_assert(!true);
            }
        }
    }
    else if (!cluster.empty())
    {
        Monitor* pMonitor = MonitorManager::find_monitor(cluster.c_str());
        mxb_assert(pMonitor);
        service->set_cluster(pMonitor);
    }

    service->targets_updated();

    // Configure the router as the last step. This makes sure that whenever a router is configured, the
    // service has already been fully configured with a valid configuration. Mostly this helps with cases
    // where the router inspects a part of the service (e.g. the servers it uses) when it is being configured.
    if (!service->m_router->getConfiguration().configure(params))
    {
        MXB_ERROR("%s: Failed to configure router instance.", service->name());
        service->state = State::FAILED;
        return nullptr;
    }

    auto service_ptr = service.release();
    LockGuard guard(this_unit.lock);
    this_unit.services.push_back(service_ptr);

    return service_ptr;
}

Service* Service::create(const char* name, const mxs::ConfigParameters& params)
{
    mxs::ConfigParameters unknown;
    return create(name, params, unknown);
}

Service* Service::create(const char* name, json_t* params)
{
    std::set<std::string> unknown;
    return create(name, params, unknown);
}

std::vector<Service*> Service::get_all()
{
    mxb_assert(MainWorker::is_current());
    return this_unit.services;
}

static std::string get_version_string(const mxs::ConfigParameters& params)
{
    std::string version_string = params.get_string(CN_VERSION_STRING);

    if (!version_string.empty() && version_string[0] != '5' && version_string[0] != '8')
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

void service_update_targets(const mxs::Monitor& monitor)
{
    LockGuard guard(this_unit.lock);
    for (Service* pService : this_unit.services)
    {
        if (pService->cluster() == &monitor)
        {
            pService->update_targets(monitor);
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

Service::Config::Config(SERVICE* service)
    : mxs::config::Configuration(service->name(), &s_spec)
    , m_service(service)
{
    add_native(&Config::m_v, &Values::type, &s_type);
    add_native(&Config::m_v, &Values::router, &s_router);
    add_native(&Config::m_v, &Values::user, &s_user);
    add_native(&Config::m_v, &Values::password, &s_password);
    add_native(&Config::m_v, &Values::enable_root, &s_enable_root_user);
    add_native(&Config::m_v, &Values::max_connections, &s_max_connections);
    add_native(&Config::m_v, &Values::conn_idle_timeout, &s_connection_timeout);
    add_native(&Config::m_v, &Values::net_write_timeout, &s_net_write_timeout);
    add_native(&Config::m_v, &Values::users_from_all, &s_auth_all_servers);
    add_native(&Config::m_v, &Values::strip_db_esc, &s_strip_db_esc);
    add_native(&Config::m_v, &Values::localhost_match_wildcard_host, &s_localhost_match_wildcard_host);
    add_native(&Config::m_v, &Values::version_string, &s_version_string);
    add_native(&Config::m_v, &Values::log_auth_warnings, &s_log_auth_warnings);
    add_native(&Config::m_v, &Values::session_track_trx_state, &s_session_track_trx_state);
    add_native(&Config::m_v, &Values::retain_last_statements, &s_retain_last_statements);
    add_native(&Config::m_v, &Values::session_trace, &s_session_trace);
    add_native(&Config::m_v, &Values::rank, &s_rank);
    add_native(&Config::m_v, &Values::connection_keepalive, &s_connection_keepalive);
    add_native(&Config::m_v, &Values::prune_sescmd_history, &s_prune_sescmd_history);
    add_native(&Config::m_v, &Values::disable_sescmd_history, &s_disable_sescmd_history);
    add_native(&Config::m_v, &Values::max_sescmd_history, &s_max_sescmd_history);
    add_native(&Config::m_v, &Values::idle_session_pool_time, &s_idle_session_pool_time);
    add_native(&Config::m_v, &Values::multiplex_timeout, &s_multiplex_timeout);
    add_native(&Config::m_v, &Values::user_accounts_file_path, &s_user_accounts_file);
    add_native(&Config::m_v, &Values::user_accounts_file_usage, &s_user_accounts_file_usage);

    add_native(&Config::m_log_debug, &s_log_debug);
    add_native(&Config::m_log_info, &s_log_info);
    add_native(&Config::m_log_notice, &s_log_notice);
    add_native(&Config::m_log_warning, &s_log_warning);
}

Service::Service(const std::string& name, const std::string& router_name)
    : SERVICE(name, router_name)
    , m_config(this)
{
    RoutingWorker::Data::initialize_workers();
}

Service::~Service()
{
    mxb_assert((m_refcount == 0 && !active()) || maxscale_teardown_in_progress() || state == State::FAILED);

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
        MXB_INFO("Destroying '%s'", name());
    }
}

// static
void Service::destroy(Service* service)
{
    mxb_assert(service->active());
    mxb_assert(mxs::MainWorker::is_current());
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

bool Service::launch()
{
    if (mxs::Config::get().config_check)
    {
        return true;
    }

    bool ok = true;
    auto my_listeners = Listener::find_by_service(this);

    if (!my_listeners.empty())
    {
        for (const auto& listener : my_listeners)
        {
            if (maxscale_is_shutting_down())
            {
                break;
            }

            if (!listener->listen())
            {
                ok = false;
                break;
            }
        }

        if (state == SERVICE::State::FAILED)
        {
            ok = false;
        }
        else if (ok)
        {
            state = SERVICE::State::STARTED;
            started = time(0);

            if (get_children().empty())
            {
                MXB_WARNING("Service '%s' has a listener but no servers", name());
            }
        }
    }
    else
    {
        MXB_WARNING("Service '%s' has no listeners defined.", name());
    }

    return ok;
}

// static
bool Service::launch_all()
{
    bool ok = true;
    int num_svc = this_unit.services.size();

    if (num_svc > 0)
    {
        MXB_NOTICE("Starting a total of %d services...", num_svc);
    }
    else
    {
        MXB_NOTICE("No services defined in any of the configuration files");
    }

    int curr_svc = 1;
    for (Service* service : this_unit.services)
    {
        if (service->launch())
        {
            MXB_NOTICE("Service '%s' started (%d/%d)", service->name(), curr_svc++, num_svc);
        }
        else
        {
            MXB_ERROR("Failed to start service '%s'.", service->name());
            ok = false;
        }

        if (maxscale_is_shutting_down())
        {
            break;
        }
    }

    return ok;
}

void Service::stop()
{
    for (const auto& listener : Listener::find_by_service(this))
    {
        listener->stop();
    }

    state = SERVICE::State::STOPPED;
}

void Service::start()
{
    for (const auto& listener : Listener::find_by_service(this))
    {
        listener->start();
    }

    state = SERVICE::State::STARTED;
}

bool service_remove_listener(Service* service, const char* target)
{
    bool rval = false;
    auto listener = Listener::find(target);

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
    for (const auto& listener : Listener::find_by_service(service))
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
    auto listener = Listener::find(name);
    return listener && listener->service() == service;
}

bool Service::can_be_destroyed() const
{
    const auto& data = *m_data;
    std::vector<std::string> names;

    std::transform(data.targets.begin(), data.targets.end(), std::back_inserter(names),
                   std::mem_fn(&mxs::Target::name));

    std::transform(data.filters.begin(), data.filters.end(), std::back_inserter(names),
                   std::mem_fn(&FilterDef::name));

    if (!names.empty())
    {
        MXB_ERROR("Cannot destroy service '%s', it uses the following objects: %s",
                  name(), mxb::join(names, ", ").c_str());
    }
    else
    {
        std::transform(m_parents.begin(), m_parents.end(), std::back_inserter(names),
                       std::mem_fn(&Service::name));

        auto filters = filter_depends_on_target(this);
        std::transform(filters.begin(), filters.end(), std::back_inserter(names),
                       std::mem_fn(&FilterDef::name));

        auto listeners = Listener::find_by_service(this);
        std::transform(listeners.begin(), listeners.end(), std::back_inserter(names),
                       std::mem_fn(&Listener::name));

        if (!names.empty())
        {
            MXB_ERROR("Cannot destroy service '%s', the following objects depend on it: %s",
                      name(), mxb::join(names, ", ").c_str());
        }
    }

    return names.empty();
}

bool Service::set_filters(const std::vector<std::string>& filters)
{
    bool rval = true;
    std::vector<SFilterDef> flist;
    auto protocols = m_router->protocols();
    uint64_t my_capabilities = get_module(router_name(), mxs::ModuleType::ROUTER)->module_capabilities;
    my_capabilities |= m_router->getCapabilities();

    if (config()->connection_keepalive.count())
    {
        my_capabilities |= RCAP_TYPE_REQUEST_TRACKING;
    }

    for (auto f : filters)
    {
        fix_object_name(f);

        if (auto def = filter_find(f.c_str()))
        {
            protocols = merge_protocols(protocols, def->instance()->protocols());
            flist.push_back(def);
            my_capabilities |= def->capabilities();
        }
        else
        {
            MXB_ERROR("Unable to find filter '%s' for service '%s'", f.c_str(), name());
            rval = false;
        }
    }

    if (protocols.empty())
    {
        MXB_ERROR("Filter list '%s' is not compatible with service '%s'",
                  mxb::join(filters, "|").c_str(), name());
        rval = false;
    }

    if (rval)
    {
        auto data = *m_data;
        data.filters = flist;
        m_data.assign(data);
        m_capabilities = my_capabilities;
        m_protocols = std::move(protocols);
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
            new_filters.push_back(f->name());
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
 * Check that all services have listeners
 * @return True if all services have listeners
 */
bool service_all_services_have_listeners()
{
    bool rval = true;
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        if (Listener::find_by_service(service).empty())
        {
            MXB_ERROR("Service '%s' has no listeners.", service->name());
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

        // Only check the dependency if the service doesn't use a cluster. If it uses a cluster, the
        // dependency isn't on this service but on the monitor that monitors the cluster.
        if (!service->cluster())
        {
            auto targets = service->get_children();

            if (std::find(targets.begin(), targets.end(), server) != targets.end())
            {
                rval.push_back(service);
            }
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
    m_router->getConfiguration().persist(os);
    m_config.persist_append(os, {s_type.name()});

    const auto& data = *m_data;

    std::vector<const char*> names;

    if (!data.filters.empty())
    {
        for (const auto& f : data.filters)
        {
            names.push_back(f->name());
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
        for (const auto& listener : Listener::find_by_service(service))
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
        for (const auto& listener : Listener::find_by_service(service))
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
    return static_cast<const Service*>(service)->json_parameters();
}

static json_t* service_all_listeners_json_data(const char* host, const SERVICE* service)
{
    json_t* arr = json_array();

    for (const auto& listener : Listener::find_by_service(service))
    {
        json_array_append_new(arr, listener->to_json(host));
    }

    return arr;
}

static json_t* service_listener_json_data(const char* host, const SERVICE* service, const char* name)
{
    auto listener = Listener::find(name);

    if (listener && listener->service() == service)
    {
        return listener->to_json(host);
    }

    return NULL;
}

json_t* service_attributes(const char* host, const SERVICE* svc)
{
    const Service* service = static_cast<const Service*>(svc);
    json_t* attr = json_object();

    json_object_set_new(attr, CN_ROUTER, json_string(service->router_name()));
    json_object_set_new(attr, CN_STATE, json_string(service_state_to_string(service->state)));

    if (service->router())
    {
        if (json_t* diag = service->router()->diagnostics())
        {
            json_object_set_new(attr, CN_ROUTER_DIAGNOSTICS, diag);
        }
    }

    json_object_set_new(attr, "started", json_string(http_to_date(service->started).c_str()));
    json_object_set_new(attr, "total_connections", json_integer(service->stats().n_total_conns()));
    json_object_set_new(attr, "connections", json_integer(service->stats().n_current_conns()));

    // The statistics for servers and services are located in different places in older versions. Newer
    // versions always have them in the statistics object of the attributes but they are also duplicated in
    // the service attributes for backwards compatibility.
    json_object_set_new(attr, "statistics", service->stats().to_json());

    json_object_set_new(attr, CN_SOURCE, mxs::Config::object_source_to_json(svc->name()));

    /** Add service parameters and listeners */
    json_t* params = service_parameters_to_json(service);

    json_object_set_new(attr, CN_PARAMETERS, params);
    json_object_set_new(attr, CN_LISTENERS, service_all_listeners_json_data(host, service));

    if (const auto* manager = service->user_account_manager())
    {
        if (json_t* users = manager->users_to_json())
        {
            json_object_set_new(attr, "users", users);
            time_t last_update = manager->last_update();
            json_object_set_new(attr, "users_last_update", json_string(http_to_date(last_update).c_str()));
        }
    }

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
            mxs_json_add_relation(filters, f->name(), CN_FILTERS);
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

        if (json_array_size(json_object_get(servers, CN_DATA)))
        {
            json_object_set_new(rel, CN_SERVERS, servers);
        }
        else
        {
            json_decref(servers);
        }

        if (json_array_size(json_object_get(services, CN_DATA)))
        {
            json_object_set_new(rel, CN_SERVICES, services);
        }
        else
        {
            json_decref(services);
        }
    }

    auto listeners = Listener::find_by_service(this);

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

json_t* service_relations_to_filter(const FilterDef* filter, const std::string& host, const std::string& self)
{
    json_t* rel = nullptr;
    LockGuard guard(this_unit.lock);

    for (Service* service : this_unit.services)
    {
        for (const auto& f : service->get_filters())
        {
            if (f.get() == filter)
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

json_t* Service::json_parameters() const
{
    json_t* rval = m_config.to_json();

    json_t* tmp = m_router->getConfiguration().to_json();
    json_object_update(rval, tmp);
    json_decref(tmp);

    return rval;
}

bool Service::configure(json_t* params)
{
    mxs::config::Configuration& router_cnf = m_router->getConfiguration();
    std::set<std::string> unknown;
    bool ok = true;

    // The service specification defines the following parameters but doesn't use them in its
    // mxs::config::Configuration class. The current configuration system doesn't detect that a parameter that
    // doesn't support modification at runtime is being modified and even if it did it wouldn't check the ones
    // that aren't used by the configuration.
    // TODO: Maybe do this inside the mxs::config::Configuration?
    for (auto name : {s_servers.name(), s_targets.name(), s_filters.name(), s_cluster.name()})
    {
        if (json_t* value = json_object_get(params, name.c_str()))
        {
            // Null values should be ignored
            if (!json_is_null(value))
            {
                MXB_ERROR("Parameter '%s' cannot be modified at runtime", name.c_str());
                ok = false;
            }
        }
    }

    return ok
           && m_config.specification().validate(params, &unknown)
           && router_cnf.specification().validate(params)
           && m_config.configure(params, &unknown)
           && router_cnf.configure(params);
}

uint64_t service_get_version(const SERVICE* svc, service_version_which_t which)
{
    return static_cast<const Service*>(svc)->get_version(which);
}

ServiceEndpoint::ServiceEndpoint(MXS_SESSION* session, Service* service, mxs::Component* up)
    : m_up(up)
    , m_session(session)
    , m_service(service)
    , m_upstream(this)
{
    m_service->incref();
    m_service->stats().add_client_connection();
}

ServiceEndpoint::~ServiceEndpoint()
{
    if (is_open())
    {
        close();
    }

    m_service->stats().remove_client_connection();
    m_service->decref();
}

// static
int32_t ServiceEndpoint::upstream_function(Filter* instance,
                                           mxs::Routable* session,
                                           GWBUF&& buffer,
                                           const mxs::ReplyRoute& down,
                                           const mxs::Reply& reply)
{
    ServiceEndpoint* self = reinterpret_cast<ServiceEndpoint*>(session);
    return self->send_upstream(std::move(buffer), down, reply);
}

int32_t ServiceEndpoint::send_upstream(GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxs::ReplyRoute& d = const_cast<mxs::ReplyRoute&>(down);
    d.push_back(this);
    return m_up->clientReply(std::move(buffer), d, reply);
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
    mxb_assert(!m_open);

    mxb::LogScope scope(m_service->name());
    std::vector<mxs::Endpoint*> endpoints;
    endpoints.reserve(m_down.size());
    std::transform(m_down.begin(), m_down.end(), std::back_inserter(endpoints),
                   std::mem_fn(&std::unique_ptr<mxs::Endpoint>::get));

    m_router_session = m_service->router()->newSession(m_session, endpoints);

    if (!m_router_session)
    {
        MXB_ERROR("Failed to create new router session for service '%s'. "
                  "See previous errors for more details.", m_service->name());
        return false;
    }

    m_router_session->setEndpoint(this);

    m_head = m_router_session;
    m_tail = &m_upstream;

    for (const auto& a : m_service->get_filters())
    {
        m_filters.emplace_back(a);
    }

    for (auto it = m_filters.begin(); it != m_filters.end(); ++it)
    {
        auto& f = *it;
        f.session = f.instance->newSession(m_session, m_service);

        if (!f.session)
        {
            MXB_ERROR("Failed to create filter session for '%s'", f.filter->name());

            for (auto d = m_filters.begin(); d != it; ++d)
            {
                mxb_assert(d->session);
                delete d->session;
                d->session = nullptr;
            }

            m_filters.clear();
            return false;
        }

        f.session->setEndpoint(this);
    }

    // The head of the chain currently points at the router
    mxs::Routable* chain_head = m_head;

    for (auto it = m_filters.rbegin(); it != m_filters.rend(); it++)
    {
        it->session->setDownstream(chain_head);
        it->down = chain_head;
        chain_head = it->session;
    }

    m_head = chain_head;

    // The tail is the upstream component of the service (the client DCB)
    mxs::Routable* chain_tail = m_tail;

    for (auto it = m_filters.begin(); it != m_filters.end(); it++)
    {
        it->session->setUpstream(chain_tail);
        it->up = chain_tail;
        chain_tail = it->session;
    }

    m_tail = chain_tail;
    m_router_session->setUpstream(m_tail);
    m_router_session->setUpstreamComponent(m_up);

    // The endpoint is now "connected"
    m_open = true;

    m_service->stats().add_connection();

    return true;
}

void ServiceEndpoint::close()
{
    mxb::LogScope scope(m_service->name());
    mxb_assert(m_open);

    delete m_router_session;
    m_router_session = nullptr;

    for (auto& a : m_filters)
    {
        delete a.session;
        a.session = nullptr;
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

bool ServiceEndpoint::routeQuery(GWBUF&& buffer)
{
    mxb::LogScope scope(m_service->name());
    mxb_assert(m_open);
    mxb_assert(buffer);

    // Track the number of packets sent through this service. Although the traffic can consist of multiple
    // packets in some cases, most of the time the packet count statistic is close to the real packet count.
    m_service->stats().add_packet();

    return m_head->routeQuery(std::move(buffer));
}

bool ServiceEndpoint::clientReply(GWBUF&& buffer, mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb::LogScope scope(m_service->name());
    mxb_assert(m_open);
    mxb_assert(buffer);
    return m_router_session->clientReply(std::move(buffer), down, reply);
}

bool ServiceEndpoint::handleError(mxs::ErrorType type, const std::string& error,
                                  mxs::Endpoint* down, const mxs::Reply& reply)
{
    mxb::LogScope scope(m_service->name());
    mxb_assert(m_open);
    return m_router_session->handleError(type, error, down, reply);
}

void ServiceEndpoint::endpointConnReleased(Endpoint* down)
{
    m_router_session->endpointConnReleased(down);
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

            if (srv_version > 0)
            {
                v_min = std::min(srv_version, v_min);
                v_max = std::max(srv_version, v_max);
            }
        }
    }

    return {v_min, v_max};
}

void Service::targets_updated()
{
    auto data = *m_data;

    // Now that we have the new set of targets, recalculate the servers that this service reaches as well as
    // the new routing capabilities.
    data.servers = get_servers(data.targets);
    data.target_capabilities = get_capabilities(data.targets);

    // Update the global value based on the local cached value. Since modifications to services are always
    // done on the same thread, there's no possibility of lost updates.
    m_data.assign(data);

    // Also update the servers queried by the user account manager.
    auto manager = user_account_manager();
    if (manager)
    {
        manager->set_backends(data.servers);
    }

    auto tracked_variables = m_tracked_variables;

    // TODO: At this point any variables needed by routers/filters could
    // TODO: be included.
    for (auto* dependency : Service::specification()->server_dependencies())
    {
        tracked_variables.insert(dependency->server_variable());
    }

    for (auto* server : data.servers)
    {
        for (const auto& variable : tracked_variables)
        {
            server->track_variable(variable);
        }
    }
}

bool Service::protocol_is_compatible(Service* other) const
{
    auto ours = get_target_protocols(m_protocols, m_data->targets);
    auto theirs = get_target_protocols(other->m_protocols, other->m_data->targets);
    bool ok = !merge_protocols(ours, theirs).empty();

    if (ok)
    {
        for (Service* parent : m_parents)
        {
            if (parent->protocol_is_compatible(other))
            {
                MXB_ERROR("Protocol mismatch: service '%s' which uses "
                          "service '%s' is not compatible with service '%s'.",
                          parent->name(), name(), other->name());
                ok = false;
            }
        }
    }
    else
    {
        MXB_ERROR("Protocol mismatch: service '%s' supports '%s' but service '%s' requires '%s'.",
                  name(), mxb::join(ours).c_str(), other->name(), mxb::join(theirs).c_str());
    }

    return ok;
}

bool Service::protocol_is_compatible(const mxs::ProtocolModule& module) const
{
    auto protocol = module.protocol_name();
    auto ours = get_target_protocols(m_protocols, m_data->targets);
    bool ok = ours.count(protocol) || ours.count(MXS_ANY_PROTOCOL);

    if (!ok)
    {
        MXB_ERROR("Protocol mismatch: cannot use '%s' with service '%s' which requires '%s'.",
                  protocol.c_str(), name(), mxb::join(ours).c_str());
    }

    return ok;
}

void Service::propagate_target_update()
{
    targets_updated();

    for (Service* service : m_parents)
    {
        service->propagate_target_update();
    }
}

void Service::remove_target(SERVER* target)
{
    auto data = *m_data;
    data.targets.erase(std::remove(data.targets.begin(), data.targets.end(), target), data.targets.end());
    m_data.assign(data);

    propagate_target_update();
}

void Service::remove_target(Service* target)
{
    auto data = *m_data;
    data.targets.erase(std::remove(data.targets.begin(), data.targets.end(), target), data.targets.end());
    m_data.assign(data);

    propagate_target_update();
    target->remove_parent(this);
}

void Service::add_target(SERVER* target)
{
    if (std::find(begin(m_data->targets), end(m_data->targets), target) == end(m_data->targets))
    {
        auto data = *m_data;
        data.targets.push_back(target);
        m_data.assign(data);

        propagate_target_update();
    }
}

void Service::add_target(Service* target)
{
    auto data = *m_data;
    data.targets.push_back(target);
    m_data.assign(data);

    target->add_parent(this);
    propagate_target_update();
}

void Service::update_targets(const Monitor& mon)
{
    mxb_assert(mxs::MainWorker::is_current());
    const auto& servers = mon.active_routing_servers();
    auto data = *m_data;
    data.targets.assign(servers.begin(), servers.end());
    m_data.assign(data);

    propagate_target_update();
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

const UserAccountManager* Service::user_account_manager() const
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

    const auto& config = *m_config.values();
    user_manager->set_credentials(config.user, config.password);
    user_manager->set_backends(m_data->servers);
    user_manager->set_union_over_backends(config.users_from_all);
    user_manager->set_strip_db_esc(config.strip_db_esc);
    user_manager->set_user_accounts_file(config.user_accounts_file_path, config.user_accounts_file_usage);
    user_manager->set_service(this);
    m_usermanager = std::move(user_manager);
    // Message each routingworker to initialize their own user caches. Wait for completion so that
    // the admin thread and workers see the same object.
    mxb::Semaphore sem;
    auto init_cache = [this]() {
            init_for(RoutingWorker::get_current());
        };

    // The task is broadcasted to all running workers. Dormant ones, should they be
    // started, will end up doing this when they are activated.
    auto n_threads = mxs::RoutingWorker::broadcast(init_cache, &sem, mxb::Worker::EXECUTE_AUTO);
    sem.wait_n(n_threads);

    m_usermanager->start();
}

void Service::init_for(RoutingWorker* pWorker)
{
    mxb_assert(RoutingWorker::get_current() == pWorker);
    mxb_assert(!*m_usercache);
    if (m_usermanager)
    {
        *m_usercache = m_usermanager->create_user_account_cache();
    }
}

void Service::finish_for(RoutingWorker* pWorker)
{
    mxb_assert(RoutingWorker::get_current() == pWorker);
    if (m_usermanager)
    {
        mxb_assert(*m_usercache);
        (*m_usercache).reset();
    }
}

void Service::request_user_account_update()
{
    user_account_manager()->update_user_accounts();
}

void Service::sync_user_account_caches()
{
    // Message each routingworker to update their caches.
    //
    // The sync must wait for the update to be applied on all RoutingWorkers. This has to be done as it's
    // possible that the service is being destroyed by the MainWorker while this function is called by the
    // updater thread. This is safe as the MainWorker will wait for the updater thread to return before
    // deleting the service.
    auto update_cache = [this]() {
            auto& user_cache = *m_usercache;
            if (user_cache)
            {
                user_cache->update_from_master();
            }
            wakeup_sessions_waiting_userdata();
        };
    mxs::RoutingWorker::execute_concurrently(update_cache);
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

        // Add custom version suffix if the normal version also exists. Check that the suffix is not
        // already somewhere in the string, as various service-as-a-server etc setups can lead to
        // multiple similar suffixes being added.
        if (!m_custom_version_suffix.empty() && !rval.empty()
            && rval.find(m_custom_version_suffix) == string::npos)
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

Router* SERVICE::router() const
{
    return m_router.get();
}

void SERVICE::track_variables(const std::set<std::string>& variables)
{
    mxb_assert(MainWorker::is_current());

    m_tracked_variables.insert(variables.begin(), variables.end());
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

bool Service::log_is_enabled(int level) const
{
    return m_log_level.load(std::memory_order_relaxed) & (1 << level);
}

namespace
{

bool configure_one_parameter(Service* service, const std::string& key, json_t* value)
{
    json_t* json = json_pack("{s: {s: {s: {s: o}}}}",
                             "data", "attributes", "parameters", key.c_str(), value);

    // Ensure the saving of runtime changes is disabled.
    auto& config = mxs::Config::get();
    bool persist_runtime_changes = std::exchange(config.persist_runtime_changes, false);

    bool ok = runtime_alter_service_from_json(service, json);
    json_decref(json);

    // Restore the situation.
    config.persist_runtime_changes = persist_runtime_changes;

    return ok;
}
}

void Service::check_server_dependencies(const std::set<std::string>& parameters)
{
    mxb_assert(MainWorker::is_current());

    // TODO: At this point any variables needed by routers/filters could
    // TODO: be included.
    auto dependencies = Service::specification()->server_dependencies();

    auto servers = reachable_servers();

    for (const auto* dependency : dependencies)
    {
        if (parameters.find(dependency->parameter().name()) != parameters.end())
        {
            const string& variable = dependency->server_variable();
            vector<string> values;

            for (const auto* server : servers)
            {
                auto value = server->get_variable_value(variable);

                if (!value.empty())
                {
                    values.push_back(value);
                }
                else
                {
                    MXB_INFO("Service '%s' depends on server variable '%s', but it has not "
                             "been fetched from the server %s.",
                             name(), variable.c_str(), server->name());
                }
            }

            string parameter = dependency->parameter().name();

            if (!values.empty())
            {
                // TODO: Would be better to collect all settings and then make one
                // TODO: call to Service::configure().
                if (!configure_one_parameter(this, parameter, dependency->apply_json(values)))
                {
                    MXB_WARNING("Could not set '%s.%s' using the value '%s'.",
                                name(), parameter.c_str(), dependency->apply(values).c_str());
                }
            }
            else
            {
                MXB_INFO("No values found, not adjusting '%s.%s'.", name(), parameter.c_str());
            }
        }
    }
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
    const auto& servers = monitor->active_routing_servers();
    auto data = *m_data;
    data.targets.assign(servers.begin(), servers.end());
    m_data.assign(data);

    m_monitor = monitor;
}

bool Service::change_cluster(mxs::Monitor* monitor)
{
    bool rval = false;

    if (m_monitor == nullptr && m_data->targets.empty())
    {
        set_cluster(monitor);
        propagate_target_update();
        rval = true;
    }

    return rval;
}

bool Service::remove_cluster(mxs::Monitor* monitor)
{
    bool rval = false;

    if (m_monitor == monitor)
    {
        auto data = *m_data;
        data.targets.clear();
        m_data.assign(data);

        propagate_target_update();
        m_monitor = nullptr;
        rval = true;
    }

    return rval;
}

int SERVICE::Config::log_levels() const
{
    int logs = 0;

    logs |= m_log_debug ? 1 << LOG_DEBUG : 0;
    logs |= m_log_info ? 1 << LOG_INFO : 0;
    logs |= m_log_notice ? 1 << LOG_NOTICE : 0;
    logs |= m_log_warning ? 1 << LOG_WARNING : 0;

    return logs;
}

bool SERVICE::Config::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    m_values.assign(m_v);
    return m_service->post_configure();
}

bool Service::post_configure()
{
    const auto& config = *m_config.values();

    // If the parameter affects the user account manager, update its settings.
    if (m_usermanager)
    {
        m_usermanager->set_credentials(config.user, config.password);
        m_usermanager->set_union_over_backends(config.users_from_all);
        m_usermanager->set_strip_db_esc(config.strip_db_esc);
    }

    m_log_level.store(m_config.log_levels(), std::memory_order_relaxed);

    return true;
}

const std::set<std::string>& Service::protocols() const
{
    return m_protocols;
}
