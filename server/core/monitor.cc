/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file monitor.c  - The monitor module management routines
 */
#include <maxscale/monitor.hh>

#include <atomic>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <chrono>
#include <string>
#include <sstream>
#include <set>
#include <zlib.h>
#include <sys/stat.h>
#include <vector>
#include <mutex>
#include <mysql.h>
#include <mysqld_error.h>
#include <errmsg.h>

#include <maxbase/externcmd.hh>
#include <maxbase/format.hh>
#include <maxbase/json.hh>
#include <maxscale/http.hh>
#include <maxscale/json_api.hh>
#include <maxscale/listener.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/paths.hh>
#include <maxscale/protocol/mariadb/maxscale.hh>
#include <maxscale/secrets.hh>

#include "internal/config.hh"
#include "internal/modules.hh"
#include "internal/server.hh"
#include "internal/service.hh"

using std::string;
using std::set;
using std::unique_ptr;
using std::vector;
using Guard = std::lock_guard<std::mutex>;
using maxscale::Monitor;
using maxscale::MonitorServer;
using ConnectResult = maxscale::MonitorServer::ConnectResult;
using namespace std::literals::chrono_literals;
using std::chrono::duration_cast;
using std::chrono::seconds;
using std::chrono::milliseconds;

const char CN_BACKEND_CONNECT_ATTEMPTS[] = "backend_connect_attempts";
const char CN_BACKEND_CONNECT_TIMEOUT[] = "backend_connect_timeout";
const char CN_BACKEND_READ_TIMEOUT[] = "backend_read_timeout";
const char CN_BACKEND_WRITE_TIMEOUT[] = "backend_write_timeout";
const char CN_DISK_SPACE_CHECK_INTERVAL[] = "disk_space_check_interval";
const char CN_EVENTS[] = "events";
const char CN_JOURNAL_MAX_AGE[] = "journal_max_age";
const char CN_MONITOR_INTERVAL[] = "monitor_interval";
const char CN_SCRIPT[] = "script";
const char CN_SCRIPT_TIMEOUT[] = "script_timeout";

namespace
{
void log_output(const std::string& cmd, const std::string& str);

namespace cfg = mxs::config;
using namespace std::chrono_literals;

class MonitorSpec : public cfg::Specification
{
public:
    using cfg::Specification::Specification;

private:
    template<class Params>
    bool do_post_validate(const cfg::Configuration* config, Params& params) const;

    bool post_validate(const cfg::Configuration* config,
                       const mxs::ConfigParameters& params,
                       const std::map<std::string, mxs::ConfigParameters>&) const override
    {
        return do_post_validate(config, params);
    }

    bool post_validate(const cfg::Configuration* config,
                       json_t* json,
                       const std::map<std::string, json_t*>&) const override
    {
        return do_post_validate(config, json);
    }
};

MonitorSpec s_spec(CN_MONITORS, cfg::Specification::MONITOR);

cfg::ParamString s_type(&s_spec, CN_TYPE, "The type of the object", CN_MONITOR);
cfg::ParamModule s_module(&s_spec, CN_MODULE, "The monitor to use", mxs::ModuleType::MONITOR);

cfg::ParamServerList s_servers(
    &s_spec, "servers", "List of servers to use",
    cfg::Param::OPTIONAL, cfg::Param::AT_RUNTIME);

cfg::ParamString s_user(
    &s_spec, "user", "Username used to monitor the servers",
    cfg::Param::AT_RUNTIME);

cfg::ParamPassword s_password(
    &s_spec, "password", "Password for the user used to monitor the servers",
    cfg::Param::AT_RUNTIME);

cfg::ParamMilliseconds s_monitor_interval(
    &s_spec, CN_MONITOR_INTERVAL, "How often the servers are monitored",
    2000ms, cfg::Param::AT_RUNTIME);

cfg::ParamSeconds s_backend_connect_timeout(
    &s_spec, CN_BACKEND_CONNECT_TIMEOUT, "Connection timeout for monitor connections",
    3s, cfg::Param::AT_RUNTIME);

cfg::ParamSeconds s_backend_read_timeout(
    &s_spec, CN_BACKEND_READ_TIMEOUT, "Read timeout for monitor connections",
    3s, cfg::Param::AT_RUNTIME);

cfg::ParamSeconds s_backend_write_timeout(
    &s_spec, CN_BACKEND_WRITE_TIMEOUT, "Write timeout for monitor connections",
    3s, cfg::Param::AT_RUNTIME);

cfg::ParamCount s_backend_connect_attempts(
    &s_spec, CN_BACKEND_CONNECT_ATTEMPTS, "Number of connection attempts to make to a server",
    1, cfg::Param::AT_RUNTIME);

cfg::ParamSeconds s_journal_max_age(
    &s_spec, CN_JOURNAL_MAX_AGE, "The time the on-disk cached server states are valid for",
    28800s, cfg::Param::AT_RUNTIME);

cfg::ParamString s_disk_space_threshold(
    &s_spec, CN_DISK_SPACE_THRESHOLD, "Disk space threshold",
    "", cfg::Param::AT_RUNTIME);

cfg::ParamMilliseconds s_disk_space_check_interval(
    &s_spec, CN_DISK_SPACE_CHECK_INTERVAL, "How often the disk space is checked",
    0ms, cfg::Param::AT_RUNTIME);

cfg::ParamString s_script(
    &s_spec, CN_SCRIPT, "Script to run whenever an event occurs",
    "", cfg::Param::AT_RUNTIME);

cfg::ParamSeconds s_script_timeout(
    &s_spec, CN_SCRIPT_TIMEOUT, "Timeout for the script",
    90s, cfg::Param::AT_RUNTIME);

cfg::ParamEnumMask<mxs_monitor_event_t> s_events(
    &s_spec, CN_EVENTS, "Events that cause the script to be called",
    {
        {ALL_EVENTS, "all"},
        {MASTER_DOWN_EVENT, "master_down"},
        {MASTER_UP_EVENT, "master_up"},
        {SLAVE_DOWN_EVENT, "slave_down"},
        {SLAVE_UP_EVENT, "slave_up"},
        {SERVER_DOWN_EVENT, "server_down"},
        {SERVER_UP_EVENT, "server_up"},
        {SYNCED_DOWN_EVENT, "synced_down"},
        {SYNCED_UP_EVENT, "synced_up"},
        {DONOR_DOWN_EVENT, "donor_down"},
        {DONOR_UP_EVENT, "donor_up"},
        {LOST_MASTER_EVENT, "lost_master"},
        {LOST_SLAVE_EVENT, "lost_slave"},
        {LOST_SYNCED_EVENT, "lost_synced"},
        {LOST_DONOR_EVENT, "lost_donor"},
        {NEW_MASTER_EVENT, "new_master"},
        {NEW_SLAVE_EVENT, "new_slave", },
        {NEW_SYNCED_EVENT, "new_synced"},
        {NEW_DONOR_EVENT, "new_donor"},
    }, ALL_EVENTS, cfg::Param::AT_RUNTIME);

class ThisUnit
{
public:
    /**
     * Mark a monitor as the monitor of the server. A server may only be monitored by one monitor.
     *
     * @param server Server to claim. The name is not checked to be a valid server name.
     * @param new_owner Monitor which claims the server
     * @param existing_owner If server is already monitored, the owning monitor name is written here
     * @return True if success, false if server was claimed by another monitor
     */
    bool claim_server(const string& server, const string& new_owner, string* existing_owner)
    {
        mxb_assert(Monitor::is_main_worker());
        bool claim_success = false;
        auto iter = m_server_owners.find(server);
        if (iter != m_server_owners.end())
        {
            // Server is already claimed by a monitor.
            *existing_owner = iter->second;
        }
        else
        {
            m_server_owners[server] = new_owner;
            claim_success = true;
        }
        return claim_success;
    }

    /**
     * Return the current monitor of a server.
     *
     * @param  server The server.
     *
     * @return The name of the owning monitor. Empty string if it does not have an owner.
     */
    string get_owner(const string& server) const
    {
        mxb_assert(Monitor::is_main_worker());

        string owner;

        auto iter = m_server_owners.find(server);
        if (iter != m_server_owners.end())
        {
            // Server is claimed by a monitor.
            owner = iter->second;
        }

        return owner;
    }

    string get_owner(SERVER* server) const
    {
        return get_owner(server->name());
    }

    /**
     * Mark a server as unmonitored.
     *
     * @param server The server name
     */
    void release_server(const string& server)
    {
        mxb_assert(Monitor::is_main_worker());
        auto iter = m_server_owners.find(server);
        mxb_assert(iter != m_server_owners.end());
        m_server_owners.erase(iter);
    }


    string claimed_by(const string& server)
    {
        mxb_assert(Monitor::is_main_worker());
        string rval;
        auto iter = m_server_owners.find(server);
        if (iter != m_server_owners.end())
        {
            rval = iter->second;
        }
        return rval;
    }

private:
    // Global map of servername->monitorname. Not mutexed, as this should only be accessed
    // from the admin thread.
    std::map<string, string> m_server_owners;
};

ThisUnit this_unit;

template<class Params>
bool MonitorSpec::do_post_validate(const cfg::Configuration* config, Params& params) const
{
    bool ok = true;

    std::string threshold = s_disk_space_threshold.get(params);

    if (!threshold.empty())
    {
        DiskSpaceLimits limit;
        ok = config_parse_disk_space_threshold(&limit, threshold.c_str());
    }

    auto script = s_script.get(params);

    if (!script.empty())
    {
        auto script_timeout = s_script_timeout.get(params);

        auto cmd = mxb::ExternalCmd::create(script, script_timeout.count(), log_output);

        if (!cmd)
        {
            ok = false;
        }
    }

    auto servers = s_servers.get(params);

    for (::SERVER* server : servers)
    {
        string owner = this_unit.get_owner(server);

        if (!owner.empty())
        {
            if (config)
            {
                // Logically, an attempt to add a server to the monitor that already monitors
                // it should be an error. However, if that approach is followed, due to the
                // way things are currently implemented, it would no longer be possible to alter
                // the servers of a monitor.
                if (owner != config->name())
                {
                    MXB_ERROR("Server '%s' is already monitored by '%s', cannot add it to '%s'.",
                              server->name(), owner.c_str(), config->name().c_str());
                    ok = false;
                }
            }
            else
            {
                MXB_ERROR("Server '%s' is already monitored by '%s', cannot add it to another monitor.",
                          server->name(), owner.c_str());
                ok = false;
            }
        }
    }

    return ok;
}

const char journal_name[] = "monitor.dat";
const char journal_template[] = "%s/%s/%s";

const char ERR_CANNOT_MODIFY[] =
    "The server is monitored, so only the maintenance status can be "
    "set/cleared manually. Status was not modified.";

/**
 * Helper class for running monitor in a worker-thread.
 */
class MonitorWorker : public maxbase::Worker
{
public:
    MonitorWorker(const MonitorWorker&) = delete;
    MonitorWorker& operator=(const MonitorWorker&) = delete;

    MonitorWorker(Monitor& monitor)
        : m_monitor(monitor)
    {
    }
private:
    Monitor& m_monitor;

    bool pre_run() override final
    {
        m_monitor.pre_run();
        return true;
    }

    void post_run() override final
    {
        m_monitor.post_run();
    }
};

const char* skip_whitespace(const char* ptr)
{
    while (*ptr && isspace(*ptr))
    {
        ptr++;
    }

    return ptr;
}

const char* skip_prefix(const char* str)
{
    const char* ptr = strchr(str, ':');
    mxb_assert(ptr);

    ptr++;
    return skip_whitespace(ptr);
}

void log_output(const std::string& cmd, const std::string& str)
{
    int err;

    if (mxs_pcre2_simple_match("(?i)^[[:space:]]*alert[[:space:]]*[:]",
                               str.c_str(),
                               0,
                               &err) == MXS_PCRE2_MATCH)
    {
        MXB_ALERT("%s: %s", cmd.c_str(), skip_prefix(str.c_str()));
    }
    else if (mxs_pcre2_simple_match("(?i)^[[:space:]]*error[[:space:]]*[:]",
                                    str.c_str(),
                                    0,
                                    &err) == MXS_PCRE2_MATCH)
    {
        MXB_ERROR("%s: %s", cmd.c_str(), skip_prefix(str.c_str()));
    }
    else if (mxs_pcre2_simple_match("(?i)^[[:space:]]*warning[[:space:]]*[:]",
                                    str.c_str(),
                                    0,
                                    &err) == MXS_PCRE2_MATCH)
    {
        MXB_WARNING("%s: %s", cmd.c_str(), skip_prefix(str.c_str()));
    }
    else if (mxs_pcre2_simple_match("(?i)^[[:space:]]*notice[[:space:]]*[:]",
                                    str.c_str(),
                                    0,
                                    &err) == MXS_PCRE2_MATCH)
    {
        MXB_NOTICE("%s: %s", cmd.c_str(), skip_prefix(str.c_str()));
    }
    else if (mxs_pcre2_simple_match("(?i)^[[:space:]]*(info|debug)[[:space:]]*[:]",
                                    str.c_str(),
                                    0,
                                    &err) == MXS_PCRE2_MATCH)
    {
        MXB_INFO("%s: %s", cmd.c_str(), skip_prefix(str.c_str()));
    }
    else
    {
        // No special format, log as notice level message
        MXB_NOTICE("%s: %s", cmd.c_str(), skip_whitespace(str.c_str()));
    }
}
}

namespace maxscale
{
Monitor::Monitor(const string& name, const string& module)
    : m_name(name)
    , m_module(module)
    , m_worker(std::make_unique<MonitorWorker>(*this))
    , m_callable(m_worker.get())
    , m_settings(name, this)
    , m_loop_called(get_time_ms())
{
}

const char* Monitor::name() const
{
    return m_name.c_str();
}

// static
cfg::Specification* Monitor::specification()
{
    return &s_spec;
}

Monitor::Settings::Settings(const std::string& name, Monitor* monitor)
    : mxs::config::Configuration(name, &s_spec)
    , m_monitor(monitor)
{
    using C = MonitorServer::ConnectionSettings;

    add_native(&Settings::type, &s_type);
    add_native(&Settings::module, &s_module);
    add_native(&Settings::servers, &s_servers);
    add_native(&Settings::interval, &s_monitor_interval);
    add_native(&Settings::events, &s_events);
    add_native(&Settings::journal_max_age, &s_journal_max_age);
    add_native(&Settings::script, &s_script);
    add_native(&Settings::script_timeout, &s_script_timeout);
    add_native(&Settings::disk_space_threshold, &s_disk_space_threshold);
    add_native(&Settings::disk_space_check_interval, &s_disk_space_check_interval);
    add_native(&Settings::conn_settings, &C::read_timeout, &s_backend_read_timeout);
    add_native(&Settings::conn_settings, &C::write_timeout, &s_backend_write_timeout);
    add_native(&Settings::conn_settings, &C::connect_timeout, &s_backend_connect_timeout);
    add_native(&Settings::conn_settings, &C::connect_attempts, &s_backend_connect_attempts);
    add_native(&Settings::conn_settings, &C::username, &s_user);
    add_native(&Settings::conn_settings, &C::password, &s_password);
}

bool Monitor::Settings::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    shared.conn_settings = conn_settings;
    config_parse_disk_space_threshold(&shared.monitor_disk_limits, disk_space_threshold.c_str());

    return m_monitor->post_configure();
}

bool Monitor::post_configure()
{
    bool ok = true;

    m_journal_max_save_interval = std::min(5 * 60L, m_settings.journal_max_age.count() / 2);

    if (m_settings.script.empty())
    {
        // Reset current external cmd if any.
        m_scriptcmd.reset();
    }
    else
    {
        m_scriptcmd = mxb::ExternalCmd::create(m_settings.script, m_settings.script_timeout.count(),
                                               log_output);
        if (!m_scriptcmd)
        {
            MXB_ERROR("Failed to initialize script '%s'.", m_settings.script.c_str());
            ok = false;
        }
    }

    // Need to start from a clean slate as servers may have been removed. If configuring the monitor fails,
    // linked services end up using obsolete targets. This should be ok, as SERVER-objects are never deleted.
    release_all_servers();
    if (ok && !prepare_servers())
    {
        ok = false;
    }
    return ok;
}

mxs::config::Configuration& Monitor::base_configuration()
{
    return m_settings;
}

const mxs::ConfigParameters& Monitor::parameters() const
{
    return m_parameters;
}

const Monitor::Settings& Monitor::settings() const
{
    return m_settings;
}

const MonitorServer::ConnectionSettings& Monitor::conn_settings() const
{
    return m_settings.shared.conn_settings;
}

long Monitor::ticks() const
{
    return m_ticks.load(std::memory_order_acquire);
}

const char* Monitor::state_string() const
{
    return is_running() ? "Running" : "Stopped";
}

Monitor::~Monitor()
{
    m_callable.cancel_dcalls();
}

/**
 * Prepare to monitor servers. Called after config changed.
 *
 * @return True on success
 */
bool Monitor::prepare_servers()
{
    // This should only be called from the admin thread while the monitor is stopped.
    mxb_assert(!is_running() && is_main_worker());

    bool claim_ok = true;
    const auto& cfg_servers = m_settings.servers;
    for (auto* srv : cfg_servers)
    {
        string existing_owner;
        if (!this_unit.claim_server(srv->name(), m_name, &existing_owner))
        {
            claim_ok = false;
            MXB_ERROR("Server '%s' is already monitored by '%s', cannot add it to '%s'.",
                      srv->name(), existing_owner.c_str(), m_name.c_str());
        }
    }

    if (claim_ok)
    {
        m_conf_servers = cfg_servers;
        configured_servers_updated(m_conf_servers);
    }
    else
    {
        // Release any claimed servers.
        for (auto& srv : cfg_servers)
        {
            if (this_unit.claimed_by(srv->name()) == m_name)
            {
                this_unit.release_server(srv->name());
            }
        }
    }

    return claim_ok;
}

void Monitor::set_active_servers(std::vector<MonitorServer*>&& servers, SetRouting routing)
{
    mxb_assert(!is_running() && is_main_worker());
    m_servers = std::move(servers);

    if (routing == SetRouting::YES)
    {
        auto n_servers = m_servers.size();
        std::vector<SERVER*> new_routing_servers;
        new_routing_servers.resize(n_servers);
        for (size_t i = 0; i < n_servers; i++)
        {
            new_routing_servers[i] = m_servers[i]->server;
        }

        set_routing_servers(std::move(new_routing_servers));
    }
}

void Monitor::set_routing_servers(std::vector<SERVER*>&& servers)
{
    {
        Guard guard(m_routing_servers_lock);
        m_routing_servers = std::move(servers);
    }

    // Update any services which use this monitor as a source of routing targets. Monitors are never
    // deleted so sending *this* to another thread is ok.
    mxs::MainWorker::get()->execute([this]() {
        active_servers_updated();
    }, mxb::Worker::EXECUTE_AUTO);
}

void Monitor::active_servers_updated()
{
    mxb_assert(is_main_worker());
    service_update_targets(this);
}

const std::vector<MonitorServer*>& Monitor::active_servers() const
{
    // Should only be called by the current monitor thread.
    mxb_assert(mxb::Worker::get_current() == m_worker.get());
    return m_servers;
}

std::vector<SERVER*> Monitor::active_routing_servers() const
{
    Guard guard(m_routing_servers_lock);
    return m_routing_servers;
}

/**
 * Release all configured servers.
 */
void Monitor::release_all_servers()
{
    // This should only be called from the admin thread while the monitor is stopped.
    mxb_assert(!is_running() && is_main_worker());

    configured_servers_updated({});
    for (auto srv : m_conf_servers)
    {
        mxb_assert(this_unit.claimed_by(srv->name()) == m_name);
        this_unit.release_server(srv->name());
    }
    m_conf_servers.clear();
}

json_t* Monitor::to_json(const char* host) const
{
    const char CN_MONITOR_DIAGNOSTICS[] = "monitor_diagnostics";
    const char CN_TICKS[] = "ticks";

    // This function mostly reads settings-type data, which is only written to by the admin thread,
    // The rest is safe to read without mutexes.
    mxb_assert(Monitor::is_main_worker());
    json_t* rval = json_object();
    json_t* attr = json_object();
    json_t* rel = json_object();

    auto my_name = name();
    json_object_set_new(rval, CN_ID, json_string(my_name));
    json_object_set_new(rval, CN_TYPE, json_string(CN_MONITORS));

    json_object_set_new(attr, CN_MODULE, json_string(m_module.c_str()));
    json_object_set_new(attr, CN_STATE, json_string(state_string()));
    json_object_set_new(attr, CN_TICKS, json_integer(ticks()));
    json_object_set_new(attr, CN_SOURCE, mxs::Config::object_source_to_json(name()));

    /** Monitor parameters */
    json_object_set_new(attr, CN_PARAMETERS, parameters_to_json());

    if (is_running())
    {
        json_t* diag = diagnostics();
        if (diag)
        {
            json_object_set_new(attr, CN_MONITOR_DIAGNOSTICS, diag);
        }
    }

    std::string self = std::string(MXS_JSON_API_MONITORS) + name() + "/relationships/";

    if (!m_servers.empty())
    {
        json_t* mon_rel = mxs_json_relationship(host, self + "servers", MXS_JSON_API_SERVERS);
        for (MonitorServer* db : m_servers)
        {
            mxs_json_add_relation(mon_rel, db->server->name(), CN_SERVERS);
        }
        json_object_set_new(rel, CN_SERVERS, mon_rel);
    }

    if (auto services = service_relations_to_monitor(this, host, self + "services"))
    {
        json_object_set_new(rel, CN_SERVICES, services);
    }

    json_object_set_new(rval, CN_RELATIONSHIPS, rel);
    json_object_set_new(rval, CN_ATTRIBUTES, attr);
    json_object_set_new(rval, CN_LINKS, mxs_json_self_link(host, CN_MONITORS, my_name));
    return rval;
}

json_t* Monitor::parameters_to_json() const
{
    json_t* rval = m_settings.to_json();
    json_t* tmp = const_cast<Monitor*>(this)->configuration().to_json();
    json_object_update(rval, tmp);
    json_decref(tmp);

    // Remove the servers parameter from the JSON output: the relationship management is supposed to be done
    // with the relationship object.
    json_object_del(rval, CN_SERVERS);

    return rval;
}

json_t* Monitor::monitored_server_json_attributes(const SERVER* srv) const
{
    json_t* rval = nullptr;
    auto comp = [srv](MonitorServer* ms) {
        return ms->server == srv;
    };

    auto iter = std::find_if(m_servers.begin(), m_servers.end(), comp);
    if (iter != m_servers.end())
    {
        auto mon_srv = *iter;
        rval = json_object();
        json_object_set_new(rval, "node_id", json_integer(mon_srv->node_id));
        json_object_set_new(rval, "master_id", json_integer(mon_srv->master_id));

        const char* event_name = MonitorServer::get_event_name(mon_srv->last_event);
        json_object_set_new(rval, "last_event", json_string(event_name));
        string triggered_at = http_to_date(mon_srv->triggered_at);
        json_object_set_new(rval, "triggered_at", json_string(triggered_at.c_str()));

        if (auto extra = diagnostics(mon_srv))
        {
            json_object_update(rval, extra);
            json_decref(extra);
        }
    }
    return rval;
}

void Monitor::wait_for_status_change()
{
    mxb_assert(is_running());
    mxb_assert(Monitor::is_main_worker());

    // Store the tick count before we request the change
    auto start = ticks();

    // Set a flag so the next loop happens sooner.
    m_status_change_pending.store(true, std::memory_order_release);

    while (start == ticks())
    {
        std::this_thread::sleep_for(milliseconds(100));
    }
}

string Monitor::gen_serverlist(int status, CredentialsApproach approach)
{
    string rval;
    rval.reserve(100 * m_servers.size());

    string separator;
    for (auto mon_server : m_servers)
    {
        auto server = static_cast<Server*>(mon_server->server);
        if (status == 0 || server->status() & status)
        {
            if (approach == CredentialsApproach::EXCLUDE)
            {
                rval += separator + mxb::string_printf("[%s]:%i", server->address(), server->port());
            }
            else
            {
                string user = conn_settings().username;
                string password = conn_settings().password;
                string server_specific_monuser = server->monitor_user();
                if (!server_specific_monuser.empty())
                {
                    user = server_specific_monuser;
                    password = server->monitor_password();
                }

                rval += separator + mxb::string_printf("%s:%s@[%s]:%d", user.c_str(), password.c_str(),
                                                       server->address(), server->port());
            }
            separator = ",";
        }
    }
    return rval;
}

MonitorServer* Monitor::find_parent_node(MonitorServer* target)
{
    MonitorServer* rval = NULL;

    if (target->master_id > 0)
    {
        for (MonitorServer* node : m_servers)
        {
            if (node->node_id == target->master_id)
            {
                rval = node;
                break;
            }
        }
    }

    return rval;
}

std::string Monitor::child_nodes(MonitorServer* parent)
{
    std::stringstream ss;

    if (parent->node_id > 0)
    {
        bool have_content = false;
        for (MonitorServer* node : m_servers)
        {
            if (node->master_id == parent->node_id)
            {
                if (have_content)
                {
                    ss << ",";
                }

                ss << "[" << node->server->address() << "]:" << node->server->port();
                have_content = true;
            }
        }
    }

    return ss.str();
}

int Monitor::launch_command(MonitorServer* ptr, const std::string& event_name)
{
    m_scriptcmd->reset_substituted();

    // A generator function is ran only if the matching substitution keyword is found.

    auto gen_initiator = [ptr] {
        return mxb::string_printf("[%s]:%d", ptr->server->address(), ptr->server->port());
    };

    auto gen_parent = [this, ptr] {
        string ss;
        MonitorServer* parent = find_parent_node(ptr);
        if (parent)
        {
            ss = mxb::string_printf("[%s]:%d", parent->server->address(), parent->server->port());
        }
        return ss;
    };

    m_scriptcmd->match_substitute("$INITIATOR", gen_initiator);
    m_scriptcmd->match_substitute("$PARENT", gen_parent);

    m_scriptcmd->match_substitute("$CHILDREN", [this, ptr] {
        return child_nodes(ptr);
    });

    m_scriptcmd->match_substitute("$EVENT", [&event_name] {
        return event_name;
    });

    m_scriptcmd->match_substitute("$CREDENTIALS", [this] {
        // Provides credentials for all servers.
        return gen_serverlist(0, CredentialsApproach::INCLUDE);
    });

    m_scriptcmd->match_substitute("$NODELIST", [this] {
        return gen_serverlist(SERVER_RUNNING);
    });

    m_scriptcmd->match_substitute("$LIST", [this] {
        return gen_serverlist(0);
    });

    m_scriptcmd->match_substitute("$MASTERLIST", [this] {
        return gen_serverlist(SERVER_MASTER);
    });

    m_scriptcmd->match_substitute("$SLAVELIST", [this] {
        return gen_serverlist(SERVER_SLAVE);
    });

    m_scriptcmd->match_substitute("$SYNCEDLIST", [this] {
        return gen_serverlist(SERVER_JOINED);
    });

    int rv = m_scriptcmd->run();

    string msg_part2 = mxb::string_printf("event '%s' on %s", event_name.c_str(), ptr->server->name());
    string msg_end = mxb::string_printf("Script was: '%s'", m_scriptcmd->substituted());
    auto msg_part2c = msg_part2.c_str();
    auto msg_endc = msg_end.c_str();

    if (rv == 0)
    {
        MXB_NOTICE("Executed monitor script for %s. %s", msg_part2c, msg_endc);
    }
    else if (rv == -1)
    {
        // Internal error
        MXB_ERROR("Failed to execute monitor script for %s. %s", msg_part2c, msg_endc);
    }
    else
    {
        // Script returned a non-zero value
        MXB_ERROR("Monitor script returned %d for %s. %s", rv, msg_part2c, msg_endc);
    }
    return rv;
}

string Monitor::get_server_monitor(const SERVER* server)
{
    return this_unit.claimed_by(server->name());
}

bool Monitor::is_main_worker()
{
    return mxs::MainWorker::is_current();
}

void Monitor::hangup_failed_servers()
{
    for (MonitorServer* ptr : m_servers)
    {
        if (ptr->status_changed() && (!(ptr->server->is_usable()) || !(ptr->server->is_in_cluster())))
        {
            BackendDCB::generate_hangup(ptr->server, "Server is no longer usable");
        }
    }
}

void Monitor::check_maintenance_requests()
{
    /* In theory, the admin may be modifying the server maintenance status during this function. The overall
     * maintenance flag should be read-written atomically to prevent missing a value. */
    bool was_pending = m_status_change_pending.exchange(false, std::memory_order_acq_rel);
    if (was_pending)
    {
        for (auto server : m_servers)
        {
            server->apply_status_requests();
        }
    }
}

void Monitor::detect_handle_state_changes()
{
    bool standard_events_enabled = m_scriptcmd && m_settings.events != 0;
    struct EventInfo
    {
        MonitorServer* target {nullptr};
        std::string    event_name;
    };
    std::vector<EventInfo> script_events;

    for (MonitorServer* ptr : m_servers)
    {
        if (ptr->status_changed())
        {
            mxs_monitor_event_t event = ptr->get_event_type();
            ptr->last_event = event;
            ptr->triggered_at = time(nullptr);
            ptr->log_state_change(annotate_state_change(ptr));

            if (standard_events_enabled && (event & m_settings.events))
            {
                script_events.push_back({ptr, MonitorServer::get_event_name(event)});
            }
        }
        else if (ptr->auth_status_changed())
        {
            ptr->log_state_change("");
        }


        if (m_scriptcmd)
        {
            // Handle custom events. Custom events ignore the "events"-setting, as they are (for now)
            // enabled by monitor-specific settings.
            auto custom_events = ptr->new_custom_events();
            for (auto& ev : custom_events)
            {
                script_events.push_back({ptr, ev});
            }
        }
    }

    for (auto& elem : script_events)
    {
        launch_command(elem.target, elem.event_name);
    }
}

void Monitor::remove_old_journal()
{
    string path = mxb::string_printf(journal_template, mxs::datadir(), name(), journal_name);
    unlink(path.c_str());
}

MonitorServer* Monitor::get_monitored_server(SERVER* search_server)
{
    mxb_assert(mxs::MainWorker::is_current());
    mxb_assert(search_server);
    for (const auto iter : m_servers)
    {
        if (iter->server == search_server)
        {
            return iter;
        }
    }
    return nullptr;
}

std::pair<bool, std::vector<MonitorServer*>>
Monitor::get_monitored_serverlist(const std::vector<SERVER*>& servers)
{
    bool ok = true;
    std::vector<MonitorServer*> monitored_array;

    // All servers in the array must be monitored by the given monitor.
    for (auto elem : servers)
    {
        if (MonitorServer* mon_serv = get_monitored_server(elem))
        {
            monitored_array.push_back(mon_serv);
        }
        else
        {
            MXB_ERROR("Server '%s' is not monitored by monitor '%s'.", elem->name(), name());
            ok = false;
        }
    }

    if (!ok)
    {
        monitored_array.clear();
    }

    return {ok, monitored_array};
}

bool Monitor::can_be_disabled(const MonitorServer& server, DisableType type, std::string* errmsg_out) const
{
    return true;
}

bool Monitor::set_server_status(SERVER* srv, int bit, string* errmsg_out)
{
    MonitorServer* msrv = get_monitored_server(srv);
    mxb_assert(msrv);

    if (!msrv)
    {
        MXB_ERROR("Monitor %s requested to set status of server %s that it does not monitor.",
                  name(), srv->address());
        return false;
    }

    bool written = false;

    if (is_running())
    {
        /* This server is monitored, in which case modifying any other status bit than Maintenance is
         * disallowed. */
        if (bit & ~(SERVER_MAINT | SERVER_DRAINING))
        {
            MXB_ERROR(ERR_CANNOT_MODIFY);
            if (errmsg_out)
            {
                *errmsg_out = ERR_CANNOT_MODIFY;
            }
        }
        else
        {
            /* Maintenance and being-drained are set/cleared using a special variable which the
             * monitor reads when starting the next update cycle. */
            MonitorServer::StatusRequest request;
            auto type = DisableType::DRAIN;
            if (bit & SERVER_MAINT)
            {
                request = MonitorServer::MAINT_ON;
                type = DisableType::MAINTENANCE;
            }
            else
            {
                mxb_assert(bit & SERVER_DRAINING);
                request = MonitorServer::DRAINING_ON;
            }

            if (can_be_disabled(*msrv, type, errmsg_out))
            {
                msrv->add_status_request(request);
                written = true;

                // Wait until the monitor picks up the change
                wait_for_status_change();
            }
        }
    }
    else
    {
        /* The monitor is not running, the bit can be set directly */
        srv->set_status(bit);
        written = true;
    }

    return written;
}

bool Monitor::clear_server_status(SERVER* srv, int bit, string* errmsg_out)
{
    MonitorServer* msrv = get_monitored_server(srv);
    mxb_assert(msrv);

    if (!msrv)
    {
        MXB_ERROR("Monitor %s requested to clear status of server %s that it does not monitor.",
                  name(), srv->address());
        return false;
    }

    bool written = false;

    if (is_running())
    {
        if (bit & ~(SERVER_MAINT | SERVER_DRAINING | SERVER_NEED_DNS))
        {
            MXB_ERROR(ERR_CANNOT_MODIFY);
            if (errmsg_out)
            {
                *errmsg_out = ERR_CANNOT_MODIFY;
            }
        }
        else
        {
            MonitorServer::StatusRequest request;
            if (bit & SERVER_MAINT)
            {
                request = MonitorServer::MAINT_OFF;
            }
            else if (bit & SERVER_NEED_DNS)
            {
                request = MonitorServer::DNS_DONE;
            }
            else
            {
                mxb_assert(bit & SERVER_DRAINING);
                request = MonitorServer::DRAINING_OFF;
            }

            msrv->add_status_request(request);
            written = true;

            // Wait until the monitor picks up the change
            wait_for_status_change();
        }
    }
    else
    {
        /* The monitor is not running, the bit can be cleared directly */
        srv->clear_status(bit);
        written = true;
    }

    return written;
}

void Monitor::deactivate()
{
    if (is_running())
    {
        stop();
    }
    release_all_servers();
}

bool Monitor::check_disk_space_this_tick()
{
    bool should_update_disk_space = false;
    auto check_interval = m_settings.disk_space_check_interval;

    if ((check_interval.count() > 0) && m_disk_space_checked.split() > check_interval)
    {
        should_update_disk_space = true;
        // Whether or not disk space check succeeds, reset the timer. This way, disk space is always
        // checked during the same tick for all servers.
        m_disk_space_checked.restart();
    }
    return should_update_disk_space;
}

bool Monitor::server_status_request_waiting() const
{
    return m_status_change_pending.load(std::memory_order_acquire);
}

const std::vector<SERVER*>& Monitor::configured_servers() const
{
    mxb_assert(is_main_worker());
    return m_conf_servers;
}

namespace journal_fields
{
const char FIELD_MXSVERSION[] = "maxscale_version";
const char FIELD_MODULE[] = "module";
const char FIELD_TIMESTAMP[] = "timestamp";
const char FIELD_NAME[] = "name";
const char FIELD_STATUS[] = "status";
const char FIELD_SERVERS[] = "servers";
}

void Monitor::write_journal_if_needed()
{
    if (m_journal_update_needed || (time(nullptr) - m_journal_updated > m_journal_max_save_interval))
    {
        write_journal();
    }
}

void Monitor::write_journal()
{
    using mxb::Json;
    Json data;
    data.set_string(journal_fields::FIELD_MODULE, m_module.c_str());
    auto mod = get_module(m_module, mxs::ModuleType::MONITOR);
    data.set_int(journal_fields::FIELD_MXSVERSION, mod->mxs_version);
    data.set_int(journal_fields::FIELD_TIMESTAMP, time(nullptr));

    Json servers_data(Json::Type::ARRAY);
    for (auto* db : m_servers)
    {
        servers_data.add_array_elem(db->journal_data());
    }
    data.set_object(journal_fields::FIELD_SERVERS, std::move(servers_data));

    save_monitor_specific_journal_data(data);   // Add derived class data if any
    if (!data.save(journal_filepath()))
    {
        MXB_ERROR("Failed to write journal data to disk. %s", data.error_msg().c_str());
    }
    m_journal_updated = time(nullptr);
    m_journal_update_needed = false;
}

void Monitor::read_journal()
{
    using mxb::Json;
    string journal_path = journal_filepath();
    if (access(journal_path.c_str(), F_OK) == 0)
    {
        Json data(Json::Type::JSON_NULL);
        if (data.load(journal_path))
        {
            string fail_reason;
            int64_t timestamp = data.get_int(journal_fields::FIELD_TIMESTAMP);
            int64_t version = data.get_int(journal_fields::FIELD_MXSVERSION);
            string module = data.get_string(journal_fields::FIELD_MODULE);

            if (data.ok())
            {
                auto mod = get_module(m_module, mxs::ModuleType::MONITOR);
                auto max_age = m_settings.journal_max_age.count();
                time_t age = time(nullptr) - timestamp;

                if (module != m_module)
                {
                    fail_reason = mxb::string_printf("File is for module '%s'. Current module is '%s'.",
                                                     module.c_str(), m_module.c_str());
                }
                else if (version != mod->mxs_version)
                {
                    fail_reason = mxb::string_printf("File is for MaxScale version %li. Current "
                                                     "MaxScale version is %i.", version, mod->mxs_version);
                }
                else if (age > max_age)
                {
                    fail_reason = mxb::string_printf("File is %li seconds old. Limit is %li seconds.",
                                                     age, max_age);
                }
                else
                {
                    // Journal seems valid, try to load data.
                    auto servers_data = data.get_array_elems(journal_fields::FIELD_SERVERS);
                    // Check that all server names in journal are also found in current monitor. If not,
                    // discard journal. TODO: this likely fails with XpandMon volatile servers
                    bool servers_found = true;
                    if (servers_data.size() == m_servers.size())
                    {
                        for (size_t i = 0; i < servers_data.size(); i++)
                        {
                            string jrn_srv_name = servers_data[i].get_string(journal_fields::FIELD_NAME);
                            if (jrn_srv_name != m_servers[i]->server->name())
                            {
                                servers_found = false;
                                break;
                            }
                        }
                    }
                    else
                    {
                        servers_found = false;
                    }

                    if (servers_found)
                    {
                        for (size_t i = 0; i < servers_data.size(); i++)
                        {
                            m_servers[i]->read_journal_data(servers_data[i]);
                        }

                        if (data.error_msg().empty())
                        {
                            load_monitor_specific_journal_data(data);
                        }
                    }
                    else
                    {
                        fail_reason = "Servers described in the journal are different from the ones "
                                      "configured on the current monitor.";
                    }
                }
            }

            // If an error occurred, the error description is either in the json object (read or conversion
            // error) or in 'fail_reason'. Some of the actual data fields in the journal could also be
            // missing or have invalid values, but this would require someone to manually edit the json file.
            // Such errors are not detected and may lead to weird values for one monitor tick.
            if (!fail_reason.empty() || !data.ok())
            {
                if (fail_reason.empty())
                {
                    fail_reason = data.error_msg();
                }
                MXB_WARNING("Discarding journal file '%s'. %s", journal_path.c_str(), fail_reason.c_str());
            }
        }
        else
        {
            MXB_ERROR("Failed to read monitor journal file from disk. %s", data.error_msg().c_str());
        }
    }
    // Non-existing journal file is not an error.
}

std::string Monitor::journal_filepath() const
{
    return mxb::string_printf("%s/%s_journal.json", mxs::datadir(), name());
}

void Monitor::save_monitor_specific_journal_data(mxb::Json& data)
{
}

void Monitor::load_monitor_specific_journal_data(const mxb::Json& data)
{
}

json_t* Monitor::diagnostics() const
{
    return json_object();
}

json_t* Monitor::diagnostics(MonitorServer* server) const
{
    return json_object();
}

bool Monitor::start()
{
    // This should only be called by monitor_start(). NULL worker is allowed since the main worker may
    // not exist during program start/stop.
    mxb_assert(Monitor::is_main_worker());
    mxb_assert(!is_running());
    mxb_assert(m_thread_running.load() == false);

    remove_old_journal();

    bool started = false;
    // Next tick should happen immediately.
    m_loop_called = get_time_ms() - settings().interval.count();
    if (!m_worker->start(name()))
    {
        MXB_ERROR("Failed to start worker for monitor '%s'.", name());
    }
    else
    {
        // Worker::start() waits until thread has started and completed its 'pre_run()`. This means that
        // Monitor::pre_run() has now completed and any writes should be visible.
        mxb_assert(m_thread_running.load(std::memory_order_relaxed));
        started = true;
    }
    return started;
}

void Monitor::stop()
{
    // This should only be called by monitor_stop().
    mxb_assert(Monitor::is_main_worker());
    mxb_assert(is_running());
    mxb_assert(m_thread_running.load() == true);

    m_worker->shutdown();
    m_worker->join();
    m_thread_running.store(false, std::memory_order_release);
}

std::tuple<bool, string> Monitor::soft_stop()
{
    auto [ok, errmsg] = prepare_to_stop();
    if (ok)
    {
        stop();
    }
    return {ok, errmsg};
}

std::tuple<bool, std::string> Monitor::prepare_to_stop()
{
    return {true, ""};
}

// static
int64_t Monitor::get_time_ms()
{
    timespec t;

    MXB_AT_DEBUG(int rv = ) clock_gettime(CLOCK_MONOTONIC_COARSE, &t);
    mxb_assert(rv == 0);

    return t.tv_sec * 1000 + (t.tv_nsec / 1000000);
}

void Monitor::flush_server_status()
{
    bool status_changed = false;
    for (MonitorServer* pMs : m_servers)
    {
        if (pMs->flush_status())
        {
            status_changed = true;
        }
    }

    if (status_changed)
    {
        request_journal_update();
    }
}

void SimpleMonitor::pre_loop()
{
    read_journal();
    // Add another overridable function for derived classes (e.g. pre_loop_monsimple) if required.
}

void SimpleMonitor::post_loop()
{
    write_journal();
    for (auto srv : active_servers())
    {
        srv->close_conn();
    }
}

void SimpleMonitor::pre_tick()
{
}

void SimpleMonitor::post_tick()
{
}

void SimpleMonitor::tick()
{
    check_maintenance_requests();
    pre_tick();

    bool first_tick = (ticks() == 0);

    const bool should_update_disk_space = check_disk_space_this_tick();

    for (MonitorServer* pMs : active_servers())
    {
        pMs->stash_current_status();

        ConnectResult conn_status = pMs->ping_or_connect();

        if (MonitorServer::connection_is_ok(conn_status))
        {
            pMs->maybe_fetch_variables();
            pMs->fetch_uptime();
            pMs->set_pending_status(SERVER_RUNNING);

            // Check permissions if permissions failed last time or if this is a new connection.
            if (pMs->had_status(SERVER_AUTH_ERROR) || (conn_status == ConnectResult::NEWCONN_OK))
            {
                pMs->check_permissions();
            }

            // If permissions are ok, continue.
            if (!pMs->has_status(SERVER_AUTH_ERROR))
            {
                if (should_update_disk_space && pMs->can_update_disk_space_status())
                {
                    pMs->update_disk_space_status();
                }

                update_server_status(pMs);
            }
        }
        else
        {
            pMs->clear_pending_status(MonitorServer::SERVER_DOWN_CLEAR_BITS);

            if (conn_status == ConnectResult::ACCESS_DENIED)
            {
                pMs->set_pending_status(SERVER_AUTH_ERROR);
            }

            /* Avoid spamming and only log if this is the first tick or if server was running last tick or
             * if server has started to reject the monitor. */
            if (first_tick || pMs->had_status(SERVER_RUNNING)
                || (pMs->has_status(SERVER_AUTH_ERROR) && !pMs->had_status(SERVER_AUTH_ERROR)))
            {
                pMs->log_connect_error(conn_status);
            }
        }

        auto* srv = pMs->server;
        pMs->mon_err_count = (srv->is_running() || srv->is_in_maint()) ? 0 : pMs->mon_err_count + 1;
    }

    post_tick();

    flush_server_status();
    detect_handle_state_changes();
    hangup_failed_servers();
    write_journal_if_needed();
}

void Monitor::pre_run()
{
    m_ticks.store(0, std::memory_order_release);
    m_thread_running.store(true, std::memory_order_release);
    pre_loop();
    m_callable.dcall(1ms, &Monitor::call_run_one_tick, this);
}

void Monitor::post_run()
{
    post_loop();
}

bool Monitor::call_run_one_tick()
{
    /** This is both the minimum sleep between two ticks and also the maximum time between early
     *  wakeup checks. */
    const int base_interval_ms = 100;
    int64_t now = get_time_ms();
    // Enough time has passed,
    if ((now - m_loop_called > settings().interval.count())
        // or a server status change request is waiting,
        || server_status_request_waiting()
        // or a monitor-specific condition is met.
        || immediate_tick_required())
    {
        m_loop_called = now;
        run_one_tick();
        now = get_time_ms();
    }

    int64_t ms_to_next_call = settings().interval.count() - (now - m_loop_called);
    // ms_to_next_call will be negative, if the run_one_tick() call took
    // longer than one monitor interval.
    int64_t delay = ((ms_to_next_call <= 0) || (ms_to_next_call >= base_interval_ms)) ?
        base_interval_ms : ms_to_next_call;

    m_callable.dcall(milliseconds(delay), &Monitor::call_run_one_tick, this);

    return false;
}

void Monitor::run_one_tick()
{
    tick();
    m_ticks.fetch_add(1, std::memory_order_acq_rel);
}

bool Monitor::immediate_tick_required()
{
    bool rval = false;
    if (m_immediate_tick_requested.load(std::memory_order_relaxed))
    {
        m_immediate_tick_requested.store(false, std::memory_order_relaxed);
        rval = true;
    }
    return rval;
}

void Monitor::request_immediate_tick()
{
    m_immediate_tick_requested.store(true, std::memory_order_relaxed);
}

void Monitor::request_journal_update()
{
    m_journal_update_needed = true;
}

bool Monitor::is_running() const
{
    return m_worker->event_loop_state() == mxb::Worker::EventLoop::RUNNING;
}
}

mxs::Monitor::Test::Test(mxs::Monitor* monitor)
    : m_monitor(monitor)
{
}

mxs::Monitor::Test::~Test()
{
}

void mxs::Monitor::Test::release_servers()
{
    m_monitor->release_all_servers();
}

void mxs::Monitor::Test::set_monitor_base_servers(const vector<SERVER*>& servers)
{
    m_monitor->m_settings.servers = servers;
    m_monitor->post_configure();
}
