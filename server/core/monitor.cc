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

#include <maxbase/format.hh>
#include <maxbase/json.hh>
#include <maxscale/diskspace.hh>
#include <maxscale/http.hh>
#include <maxscale/json_api.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/paths.hh>
#include <maxscale/protocol/mariadb/diskspace.hh>
#include <maxscale/protocol/mariadb/maxscale.hh>
#include <maxscale/secrets.hh>

#include "internal/config.hh"
#include "internal/externcmd.hh"
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
    // TODO: Consider dropping this altogether and simply fetch the variables at each monitor tick.
    static constexpr seconds variables_update_interval = 10s;

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

        unique_ptr<ExternalCmd> cmd = ExternalCmd::create(script, script_timeout.count());

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

/** Server type specific bits */
const uint64_t server_type_bits = SERVER_MASTER | SERVER_SLAVE | SERVER_JOINED | SERVER_RELAY | SERVER_BLR;

/** All server bits */
const uint64_t all_server_bits = SERVER_RUNNING | SERVER_MAINT | SERVER_MASTER | SERVER_SLAVE
    | SERVER_JOINED | SERVER_RELAY | SERVER_BLR;

const char journal_name[] = "monitor.dat";
const char journal_template[] = "%s/%s/%s";

bool check_disk_space_exhausted(MonitorServer* pMs,
                                const std::string& path,
                                const maxscale::disk::SizesAndName& san,
                                int32_t max_percentage)
{
    bool disk_space_exhausted = false;

    int32_t used_percentage = ((san.total() - san.available()) / (double)san.total()) * 100;

    if (used_percentage >= max_percentage)
    {
        MXB_ERROR("Disk space on %s at %s is exhausted; %d%% of the disk "
                  "mounted on the path %s has been used, and the limit it %d%%.",
                  pMs->server->name(),
                  pMs->server->address(),
                  used_percentage,
                  path.c_str(),
                  max_percentage);
        disk_space_exhausted = true;
    }

    return disk_space_exhausted;
}

const char ERR_CANNOT_MODIFY[] =
    "The server is monitored, so only the maintenance status can be "
    "set/cleared manually. Status was not modified.";
const char WRN_REQUEST_OVERWRITTEN[] =
    "Previous maintenance/draining request was not yet read by the monitor and was overwritten.";

const MonitorServer::EventList empty_event_list;

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
        m_scriptcmd = ExternalCmd::create(m_settings.script, m_settings.script_timeout.count());
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

void Monitor::set_active_servers(std::vector<MonitorServer*>&& servers)
{
    mxb_assert(!is_running() && is_main_worker());
    m_servers = std::move(servers);
    auto n_servers = m_servers.size();
    m_routing_servers.resize(n_servers);
    for (size_t i = 0; i < n_servers; i++)
    {
        m_routing_servers[i] = m_servers[i]->server;
    }
    // Update any services which use this monitor as a source of routing targets.
    active_servers_updated();
}

void Monitor::active_servers_updated()
{
    mxb_assert(!is_running() && is_main_worker());
    service_update_targets(*this);
}

const std::vector<MonitorServer*>& Monitor::active_servers() const
{
    // Should only be called by a running monitor.
    mxb_assert(mxb::Worker::get_current() == m_worker.get());
    return m_servers;
}

const std::vector<SERVER*>& Monitor::active_routing_servers() const
{
    mxb_assert(is_main_worker());
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

        const char* event_name = get_event_name(mon_srv->last_event);
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

void MonitorServer::stash_current_status()
{
    // Should be run at the start of a monitor tick to both prepare next pending status and save the previous
    // status.
    auto status = server->status();
    m_prev_status = status;
    m_pending_status = status;
}

void MonitorServer::set_pending_status(uint64_t bits)
{
    m_pending_status |= bits;
}

void MonitorServer::clear_pending_status(uint64_t bits)
{
    m_pending_status &= ~bits;
}

/*
 * Determine a monitor event, defined by the difference between the old
 * status of a server and the new status.
 *
 * @return  monitor_event_t     A monitor event (enum)
 *
 * @note This function must only be called from mon_process_state_changes
 */
mxs_monitor_event_t MonitorServer::get_event_type() const
{
    auto rval = event_type(m_prev_status, server->status());

    mxb_assert_message(rval != UNDEFINED_EVENT,
                       "No event for state transition: [%s] -> [%s]",
                       Target::status_to_string(m_prev_status, server->stats().n_current_conns()).c_str(),
                       server->status_string().c_str());

    return rval;
}

// static
mxs_monitor_event_t MonitorServer::event_type(uint64_t before, uint64_t after)
{
    typedef enum
    {
        DOWN_EVENT,
        UP_EVENT,
        LOSS_EVENT,
        NEW_EVENT,
        UNSUPPORTED_EVENT
    } general_event_type;

    general_event_type event_type = UNSUPPORTED_EVENT;

    uint64_t prev = before & all_server_bits;
    uint64_t present = after & all_server_bits;

    if (prev == present)
    {
        /* This should never happen */
        mxb_assert(false);
        return UNDEFINED_EVENT;
    }

    if ((prev & SERVER_RUNNING) == 0)
    {
        /* The server was not running previously */
        if ((present & SERVER_RUNNING) != 0)
        {
            event_type = UP_EVENT;
        }
        else
        {
            /* Otherwise, was not running and still is not running. This should never happen. */
            mxb_assert(false);
        }
    }
    else
    {
        /* Previous state must have been running */
        if ((present & SERVER_RUNNING) == 0)
        {
            event_type = DOWN_EVENT;
        }
        else
        {
            /** These are used to detect whether we actually lost something or
             * just transitioned from one state to another */
            uint64_t prev_bits = prev & (SERVER_MASTER | SERVER_SLAVE);
            uint64_t present_bits = present & (SERVER_MASTER | SERVER_SLAVE);

            /* Was running and still is */
            if ((!prev_bits || !present_bits || prev_bits == present_bits)
                && (prev & server_type_bits))
            {
                /* We used to know what kind of server it was */
                event_type = LOSS_EVENT;
            }
            else
            {
                /* We didn't know what kind of server it was, now we do*/
                event_type = NEW_EVENT;
            }
        }
    }

    mxs_monitor_event_t rval = UNDEFINED_EVENT;

    switch (event_type)
    {
    case UP_EVENT:
        rval = (present & SERVER_MASTER) ? MASTER_UP_EVENT :
            (present & SERVER_SLAVE) ? SLAVE_UP_EVENT :
            (present & SERVER_JOINED) ? SYNCED_UP_EVENT :
            (present & SERVER_RELAY) ? RELAY_UP_EVENT :
            (present & SERVER_BLR) ? BLR_UP_EVENT :
            SERVER_UP_EVENT;
        break;

    case DOWN_EVENT:
        rval = (prev & SERVER_MASTER) ? MASTER_DOWN_EVENT :
            (prev & SERVER_SLAVE) ? SLAVE_DOWN_EVENT :
            (prev & SERVER_JOINED) ? SYNCED_DOWN_EVENT :
            (prev & SERVER_RELAY) ? RELAY_DOWN_EVENT :
            (prev & SERVER_BLR) ? BLR_DOWN_EVENT :
            SERVER_DOWN_EVENT;
        break;

    case LOSS_EVENT:
        rval = (prev & SERVER_MASTER) ? LOST_MASTER_EVENT :
            (prev & SERVER_SLAVE) ? LOST_SLAVE_EVENT :
            (prev & SERVER_JOINED) ? LOST_SYNCED_EVENT :
            (prev & SERVER_RELAY) ? LOST_RELAY_EVENT :
            (prev & SERVER_BLR) ? LOST_BLR_EVENT :
            UNDEFINED_EVENT;
        break;

    case NEW_EVENT:
        rval = (present & SERVER_MASTER) ? NEW_MASTER_EVENT :
            (present & SERVER_SLAVE) ? NEW_SLAVE_EVENT :
            (present & SERVER_JOINED) ? NEW_SYNCED_EVENT :
            (present & SERVER_RELAY) ? NEW_RELAY_EVENT :
            (present & SERVER_BLR) ? NEW_BLR_EVENT :
            UNDEFINED_EVENT;
        break;

    default:
        /* This should never happen */
        mxb_assert(false);
        break;
    }

    return rval;
}

const char* Monitor::get_event_name(mxs_monitor_event_t event)
{
    static std::map<mxs_monitor_event_t, const char*> values =
    {
        {MASTER_DOWN_EVENT, "master_down", },
        {MASTER_UP_EVENT,   "master_up",   },
        {SLAVE_DOWN_EVENT,  "slave_down",  },
        {SLAVE_UP_EVENT,    "slave_up",    },
        {SERVER_DOWN_EVENT, "server_down", },
        {SERVER_UP_EVENT,   "server_up",   },
        {SYNCED_DOWN_EVENT, "synced_down", },
        {SYNCED_UP_EVENT,   "synced_up",   },
        {DONOR_DOWN_EVENT,  "donor_down",  },
        {DONOR_UP_EVENT,    "donor_up",    },
        {LOST_MASTER_EVENT, "lost_master", },
        {LOST_SLAVE_EVENT,  "lost_slave",  },
        {LOST_SYNCED_EVENT, "lost_synced", },
        {LOST_DONOR_EVENT,  "lost_donor",  },
        {NEW_MASTER_EVENT,  "new_master",  },
        {NEW_SLAVE_EVENT,   "new_slave",   },
        {NEW_SYNCED_EVENT,  "new_synced",  },
        {NEW_DONOR_EVENT,   "new_donor",   },
        {RELAY_UP_EVENT,    "relay_up",    },
        {RELAY_DOWN_EVENT,  "relay_down",  },
        {LOST_RELAY_EVENT,  "lost_relay",  },
        {NEW_RELAY_EVENT,   "new_relay",   },
        {BLR_UP_EVENT,      "blr_up",      },
        {BLR_DOWN_EVENT,    "blr_down",    },
        {LOST_BLR_EVENT,    "lost_blr",    },
        {NEW_BLR_EVENT,     "new_blr",     },
    };

    auto it = values.find(event);
    mxb_assert(it != values.end());
    return it == values.end() ? "undefined_event" : it->second;
}

const char* MonitorServer::get_event_name()
{
    return Monitor::get_event_name(last_event);
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

/**
 * Check if current monitored server status has changed.
 *
 * @return              true if status has changed
 */
bool MonitorServer::status_changed()
{
    return status_changed(m_prev_status, server->status());
}

// static
bool MonitorServer::status_changed(uint64_t before, uint64_t after)
{
    bool rval = false;

    /* Previous status is -1 if not yet set */
    if (before != static_cast<uint64_t>(-1))
    {

        uint64_t old_status = before & all_server_bits;
        uint64_t new_status = after & all_server_bits;

        /**
         * The state has changed if the relevant state bits are not the same,
         * the server is either running, stopping or starting and the server is
         * not going into maintenance or coming out of it
         */
        if (old_status != new_status
            && ((old_status | new_status) & SERVER_MAINT) == 0
            && ((old_status | new_status) & SERVER_RUNNING) == SERVER_RUNNING)
        {
            rval = true;
        }
    }

    return rval;
}

bool MonitorServer::auth_status_changed()
{
    uint64_t old_status = m_prev_status;
    uint64_t new_status = server->status();

    return old_status != static_cast<uint64_t>(-1) && old_status != new_status
           && (old_status & SERVER_AUTH_ERROR) != (new_status & SERVER_AUTH_ERROR);
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

    int rv = m_scriptcmd->externcmd_execute();

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

MonitorServer::ConnectResult
MariaServer::ping_or_connect_to_db(const MonitorServer::ConnectionSettings& sett, SERVER& server,
                                   MYSQL** ppConn, std::string* pError)
{
    mxb_assert(ppConn);
    mxb_assert(pError);
    auto pConn = *ppConn;
    if (pConn)
    {
        mxb::StopWatch timer;
        /** Return if the connection is OK */
        if (mysql_ping(pConn) == 0)
        {
            long time_us = std::chrono::duration_cast<std::chrono::microseconds>(timer.split()).count();
            server.set_ping(time_us);
            return ConnectResult::OLDCONN_OK;
        }
    }

    string uname = sett.username;
    string passwd = sett.password;
    const auto& srv = static_cast<const Server&>(server);           // Clean this up later.

    string server_specific_monuser = srv.monitor_user();
    if (!server_specific_monuser.empty())
    {
        uname = server_specific_monuser;
        passwd = srv.monitor_password();
    }

    auto dpwd = mxs::decrypt_password(passwd);

    auto connect = [&pConn, &sett, &server, &uname, &dpwd](int port) {
        if (pConn)
        {
            mysql_close(pConn);
        }
        pConn = mysql_init(nullptr);
        // ConC takes the timeouts in seconds.
        unsigned int conn_to_s = sett.connect_timeout.count();
        unsigned int read_to_s = sett.read_timeout.count();
        unsigned int write_to_s = sett.write_timeout.count();
        mysql_optionsv(pConn, MYSQL_OPT_CONNECT_TIMEOUT, &conn_to_s);
        mysql_optionsv(pConn, MYSQL_OPT_READ_TIMEOUT, &read_to_s);
        mysql_optionsv(pConn, MYSQL_OPT_WRITE_TIMEOUT, &write_to_s);
        mysql_optionsv(pConn, MYSQL_PLUGIN_DIR, mxs::connector_plugindir());
        mysql_optionsv(pConn, MARIADB_OPT_MULTI_STATEMENTS, nullptr);

        if (server.proxy_protocol())
        {
            mxq::set_proxy_header(pConn);
        }

        return mxs_mysql_real_connect(pConn, &server, port, uname.c_str(), dpwd.c_str()) != nullptr;
    };

    ConnectResult conn_result = ConnectResult::REFUSED;
    auto extra_port = server.extra_port();

    for (int i = 0; i < sett.connect_attempts && conn_result != ConnectResult::NEWCONN_OK; i++)
    {
        auto start = time(nullptr);
        if (extra_port > 0)
        {
            // Extra-port defined, try it first.
            if (connect(extra_port))
            {
                conn_result = ConnectResult::NEWCONN_OK;
            }
            else
            {
                // If extra-port connection failed due to too low max_connections or another likely
                // configuration related error, try normal port.
                auto err = mysql_errno(pConn);
                if (err == ER_CON_COUNT_ERROR || err == CR_CONNECTION_ERROR)
                {
                    if (connect(server.port()))
                    {
                        conn_result = ConnectResult::NEWCONN_OK;
                        MXB_WARNING("Could not connect with extra-port to '%s', using normal port.",
                                    server.name());
                    }
                }
            }
        }
        else if (connect(server.port()))
        {
            conn_result = ConnectResult::NEWCONN_OK;
        }

        if (conn_result == ConnectResult::REFUSED)
        {
            *pError = mysql_error(pConn);
            auto err = mysql_errno(pConn);
            mysql_close(pConn);
            pConn = nullptr;
            if (is_access_denied_error(err))
            {
                conn_result = ConnectResult::ACCESS_DENIED;
            }
            else if (difftime(time(nullptr), start) >= sett.connect_timeout.count())
            {
                conn_result = ConnectResult::TIMEOUT;
            }
        }
    }

    *ppConn = pConn;

    if (conn_result == ConnectResult::NEWCONN_OK)
    {
        // If a new connection was created, measure ping separately.
        mxb::StopWatch timer;
        long time_us = mxs::Target::PING_UNDEFINED;
        if (mysql_ping(pConn) == 0)
        {
            time_us = std::chrono::duration_cast<std::chrono::microseconds>(timer.split()).count();
        }
        server.set_ping(time_us);
    }

    return conn_result;
}

ConnectResult MariaServer::ping_or_connect()
{
    auto old_type = server->info().type();
    auto connect = [this] {
        return ping_or_connect_to_db(m_shared.conn_settings, *server, &con, &m_latest_error);
    };

    auto res = connect();
    if (res == ConnectResult::NEWCONN_OK)
    {
        mxs_mysql_update_server_version(server, con);
        if (server->info().type() != old_type)
        {
            /**
             * The server type affects the init commands sent by mxs_mysql_real_connect.
             * If server type changed, reconnect so that the correct commands are sent.
             * This typically only happens during startup.
             */
            mysql_close(con);
            con = nullptr;
            res = connect();
        }
    }
    return res;
}

bool MonitorServer::should_fetch_variables()
{
    bool rval = false;
    // Only fetch variables from real servers.
    return is_database()
           && (mxb::Clock::now() - m_last_variables_update) > this_unit.variables_update_interval;
}

/**
 * Fetch variables from the server. The values are stored in the SERVER object.
 */
bool MariaServer::fetch_variables()
{
    bool rv = true;

    auto variables = server->tracked_variables();

    if (!variables.empty())
    {
        string query = "SHOW GLOBAL VARIABLES WHERE VARIABLE_NAME IN ("
            + mxb::join(variables, ",", "'") + ")";

        string err_msg;
        unsigned int err;
        if (auto r = mxs::execute_query(con, query, &err_msg, &err))
        {
            m_last_variables_update = mxb::Clock::now();

            Server::Variables variable_values;
            while (r->next_row())
            {
                mxb_assert(r->get_col_count() == 2);

                auto variable = r->get_string(0);
                variable_values[variable] = r->get_string(1);

                variables.erase(variable);
            }

            if (mxb_log_should_log(LOG_INFO))
            {
                auto old_variables = server->get_variables();
                decltype(old_variables) changed;
                std::set_difference(variable_values.begin(), variable_values.end(),
                                    old_variables.begin(), old_variables.end(),
                                    std::inserter(changed, changed.begin()));

                if (!changed.empty())
                {
                    auto str = mxb::transform_join(changed, [](auto kv){
                        return kv.first + " = " + kv.second;
                    }, ", ", "'");

                    MXB_INFO("Variables have changed on '%s', next check in %ld seconds: %s",
                             server->name(), this_unit.variables_update_interval.count(), str.c_str());
                }
            }

            server->set_variables(std::move(variable_values));

            if (!variables.empty())
            {
                MXB_INFO("Variable(s) %s were not found.", mxb::join(variables, ", ", "'").c_str());
                mxb_assert(!true);      // Suggests typo in variable name.
            }
        }
        else
        {
            MXB_ERROR("Fetching server variables failed: (%d), %s", err, err_msg.c_str());
            mxb_assert(!true);      // Suggests error in SQL
            rv = false;
        }
    }

    return rv;
}

void MariaServer::fetch_uptime()
{
    if (auto r = mxs::execute_query(con, "SHOW STATUS LIKE 'Uptime'"))
    {
        if (r->next_row() && r->get_col_count() > 1)
        {
            server->set_uptime(r->get_int(1));
        }
    }
}

/**
 * Is the return value one of the 'OK' values.
 *
 * @param connect_result Return value of mon_ping_or_connect_to_db
 * @return True of connection is ok
 */
bool Monitor::connection_is_ok(ConnectResult connect_result)
{
    return connect_result == ConnectResult::OLDCONN_OK || connect_result == ConnectResult::NEWCONN_OK;
}

string Monitor::get_server_monitor(const SERVER* server)
{
    return this_unit.claimed_by(server->name());
}

bool Monitor::is_main_worker()
{
    return mxs::MainWorker::is_current();
}

std::string MonitorServer::get_connect_error(ConnectResult rval)
{
    mxb_assert(!Monitor::connection_is_ok(rval));
    const char TIMED_OUT[] = "Monitor timed out when connecting to server %s[%s:%d] : '%s'";
    const char REFUSED[] = "Monitor was unable to connect to server %s[%s:%d] : '%s'";
    return mxb::string_printf(rval == ConnectResult::TIMEOUT ? TIMED_OUT : REFUSED,
                              server->name(), server->address(), server->port(), m_latest_error.c_str());
}

/**
 * Log an error about the failure to connect to a backend server and why it happened.
 *
 * @param rval Return value of mon_ping_or_connect_to_db
 */
void MonitorServer::log_connect_error(ConnectResult rval)
{
    MXB_ERROR("%s", get_connect_error(rval).c_str());
}

void MonitorServer::log_state_change(const std::string& reason)
{
    string prev = Target::status_to_string(m_prev_status, server->stats().n_current_conns());
    string next = server->status_string();
    MXB_NOTICE("Server changed state: %s[%s:%u]: %s. [%s] -> [%s]%s%s",
               server->name(), server->address(), server->port(),
               get_event_name(), prev.c_str(), next.c_str(),
               reason.empty() ? "" : ": ", reason.c_str());
}

void Monitor::hangup_failed_servers()
{
    for (MonitorServer* ptr : m_servers)
    {
        if (ptr->status_changed() && (!(ptr->server->is_usable()) || !(ptr->server->is_in_cluster())))
        {
            BackendDCB::hangup(ptr->server);
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
                script_events.push_back({ptr, get_event_name(event)});
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
            MonitorServer::StatusRequest request;
            if (bit & SERVER_MAINT)
            {
                request = MonitorServer::MAINT_OFF;
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

bool MonitorServer::can_update_disk_space_status() const
{
    return m_ok_to_check_disk_space
           && (!m_shared.monitor_disk_limits.empty() || server->have_disk_space_limits());
}

void MariaServer::update_disk_space_status()
{
    auto pMs = this;    // TODO: Clean
    std::map<std::string, disk::SizesAndName> info;

    int rv = disk::get_info_by_path(pMs->con, &info);

    if (rv == 0)
    {
        // Server-specific setting takes precedence.
        auto dst = pMs->server->get_disk_space_limits();
        if (dst.empty())
        {
            dst = m_shared.monitor_disk_limits;
        }

        bool disk_space_exhausted = false;
        int32_t star_max_percentage = -1;
        std::set<std::string> checked_paths;

        for (const auto& dst_item : dst)
        {
            string path = dst_item.first;
            int32_t max_percentage = dst_item.second;

            if (path == "*")
            {
                star_max_percentage = max_percentage;
            }
            else
            {
                auto j = info.find(path);

                if (j != info.end())
                {
                    const disk::SizesAndName& san = j->second;

                    disk_space_exhausted = check_disk_space_exhausted(pMs, path, san, max_percentage);
                    checked_paths.insert(path);
                }
                else
                {
                    MXB_WARNING("Disk space threshold specified for %s even though server %s at %s"
                                "does not have that.",
                                path.c_str(),
                                pMs->server->name(),
                                pMs->server->address());
                }
            }
        }

        if (star_max_percentage != -1)
        {
            for (auto j = info.begin(); j != info.end(); ++j)
            {
                string path = j->first;

                if (checked_paths.find(path) == checked_paths.end())
                {
                    const disk::SizesAndName& san = j->second;

                    disk_space_exhausted = check_disk_space_exhausted(pMs, path, san, star_max_percentage);
                }
            }
        }

        if (disk_space_exhausted)
        {
            pMs->m_pending_status |= SERVER_DISK_SPACE_EXHAUSTED;
        }
        else
        {
            pMs->m_pending_status &= ~SERVER_DISK_SPACE_EXHAUSTED;
        }
    }
    else
    {
        SERVER* pServer = pMs->server;

        if (mysql_errno(pMs->con) == ER_UNKNOWN_TABLE)
        {
            // Disable disk space checking for this server.
            m_ok_to_check_disk_space = false;

            MXB_ERROR("Disk space cannot be checked for %s at %s, because either the "
                      "version (%s) is too old, or the DISKS information schema plugin "
                      "has not been installed. Disk space checking has been disabled.",
                      pServer->name(),
                      pServer->address(),
                      pServer->info().version_string());
        }
        else
        {
            MXB_ERROR("Checking the disk space for %s at %s failed due to: (%d) %s",
                      pServer->name(),
                      pServer->address(),
                      mysql_errno(pMs->con),
                      mysql_error(pMs->con));
        }
    }
}

bool MonitorServer::flush_status()
{
    bool status_changed = false;
    if (m_pending_status != server->status())
    {
        server->assign_status(m_pending_status);
        status_changed = true;
    }
    return status_changed;
}

bool MonitorServer::has_status(uint64_t bits) const
{
    return (m_pending_status & bits) == bits;
}

bool MonitorServer::had_status(uint64_t bits) const
{
    return (m_prev_status & bits) == bits;
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

        if (connection_is_ok(conn_status))
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

MonitorServer::MonitorServer(SERVER* server, const SharedSettings& shared)
    : server(server)
    , m_shared(shared)
{
    // Initialize 'm_last_session_track_update' so that an update is performed 1s after monitor start.
    m_last_variables_update = mxb::Clock::now() - this_unit.variables_update_interval + 1s;
}

void MonitorServer::apply_status_requests()
{
    // The admin can only modify the [Maintenance] and [Drain] bits.
    int admin_msg = m_status_request.exchange(NO_CHANGE, std::memory_order_acq_rel);
    string msg;

    switch (admin_msg)
    {
    case MonitorServer::MAINT_ON:
        if (!server->is_in_maint())
        {
            msg = mxb::string_printf("Server '%s' is going into maintenance.", server->name());
        }
        server->set_status(SERVER_MAINT);
        break;

    case MonitorServer::MAINT_OFF:
        if (server->is_in_maint())
        {
            msg = mxb::string_printf("Server '%s' is coming out of maintenance.", server->name());
        }
        server->clear_status(SERVER_MAINT);
        break;

    case MonitorServer::DRAINING_ON:
        if (!server->is_in_maint() && !server->is_draining())
        {
            msg = mxb::string_printf("Server '%s' is being drained.", server->name());
        }
        server->set_status(SERVER_DRAINING);
        break;

    case MonitorServer::DRAINING_OFF:
        if (!server->is_in_maint() && server->is_draining())
        {
            msg = mxb::string_printf("Server '%s' is no longer being drained.", server->name());
        }
        server->clear_status(SERVER_DRAINING);
        break;

    case MonitorServer::NO_CHANGE:
        break;

    default:
        mxb_assert(!true);
    }

    if (!msg.empty())
    {
        MXB_NOTICE("%s", msg.c_str());
    }
}

void MonitorServer::add_status_request(StatusRequest request)
{
    int previous_request = m_status_request.exchange(request, std::memory_order_acq_rel);
    // Warn if the previous request hasn't been read.
    if (previous_request != NO_CHANGE)
    {
        MXB_WARNING(WRN_REQUEST_OVERWRITTEN);
    }
}

bool MonitorServer::is_database() const
{
    return server->info().is_database();
}

bool MonitorServer::maybe_fetch_variables()
{
    bool rv = false;
    if (should_fetch_variables())
    {
        rv = fetch_variables();
    }
    return rv;
}

const MonitorServer::EventList& MonitorServer::new_custom_events() const
{
    return empty_event_list;
}

mxb::Json MonitorServer::journal_data() const
{
    mxb::Json rval;
    rval.set_string(journal_fields::FIELD_NAME, server->name());
    rval.set_int(journal_fields::FIELD_STATUS, server->status());
    return rval;
}

void MonitorServer::read_journal_data(const mxb::Json& data)
{
    uint64_t status = data.get_int(journal_fields::FIELD_STATUS);

    // Ignoring the AUTH_ERROR status causes the authentication error message to be logged every time MaxScale
    // is restarted. This should make it easier to spot authentication related problems during startup.
    status &= ~SERVER_AUTH_ERROR;

    m_prev_status = status;
    server->set_status(status);
}

const MonitorServer::ConnectionSettings& MonitorServer::conn_settings() const
{
    return m_shared.conn_settings;
}

bool MonitorServer::is_access_denied_error(int64_t errornum)
{
    return errornum == ER_ACCESS_DENIED_ERROR || errornum == ER_ACCESS_DENIED_NO_PASSWORD_ERROR;
}

void MariaServer::close_conn()
{
    if (con)
    {
        mysql_close(con);
        con = nullptr;
    }
}

MariaServer::MariaServer(SERVER* server, const MonitorServer::SharedSettings& shared)
    : MonitorServer(server, shared)
{
}

MariaServer::~MariaServer()
{
    close_conn();
}

void MariaServer::check_permissions()
{
    // Test with a typical query to make sure the monitor has sufficient permissions.
    auto& query = permission_test_query();
    string err_msg;
    auto result = execute_query(query, &err_msg);

    if (result == nullptr)
    {
        /* In theory, this could be due to other errors as well, but that is quite unlikely since the
         * connection was just checked. The end result is in any case that the server is not updated,
         * and that this test is retried next round. */
        set_pending_status(SERVER_AUTH_ERROR);
        // Only print error if last round was ok.
        if (!had_status(SERVER_AUTH_ERROR))
        {
            MXB_WARNING("Error during monitor permissions test for server '%s': %s",
                        server->name(), err_msg.c_str());
        }
    }
    else
    {
        clear_pending_status(SERVER_AUTH_ERROR);
    }
}

std::unique_ptr<mxb::QueryResult>
MariaServer::execute_query(const string& query, std::string* errmsg_out, unsigned int* errno_out)
{
    return maxscale::execute_query(con, query, errmsg_out, errno_out);
}

const std::string& MariaServer::permission_test_query() const
{
    mxb_assert(!true);      // Can only be empty for monitors that do not check permissions.
    static string dummy = "";
    return dummy;
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
