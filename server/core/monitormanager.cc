/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>

#include <fcntl.h>
#include <maxbase/format.hh>
#include <maxscale/json_api.hh>
#include <maxscale/paths.hh>

#include "internal/config.hh"
#include "internal/monitor.hh"
#include "internal/monitormanager.hh"
#include "internal/modules.hh"
#include "internal/externcmd.hh"

using maxscale::Monitor;
using maxscale::MonitorServer;
using Guard = std::lock_guard<std::mutex>;
using std::string;
using mxb::string_printf;

namespace
{

class ThisUnit
{
public:

    /**
     * Call a function on every monitor in the global monitor list.
     *
     * @param apply The function to apply. If the function returns false, iteration is discontinued.
     */
    void foreach_monitor(const std::function<bool(Monitor*)>& apply)
    {
        Guard guard(m_all_monitors_lock);
        for (Monitor* monitor : m_all_monitors)
        {
            if (!apply(monitor))
            {
                break;
            }
        }
    }

    /**
     * Clear the internal list and return previous contents.
     *
     * @return Contents before clearing
     */
    std::vector<Monitor*> clear()
    {
        Guard guard(m_all_monitors_lock);
        m_all_monitors.insert(m_all_monitors.end(), m_deact_monitors.begin(), m_deact_monitors.end());
        m_deact_monitors.clear();
        return std::move(m_all_monitors);
    }

    void insert_front(Monitor* monitor)
    {
        Guard guard(m_all_monitors_lock);
        m_all_monitors.insert(m_all_monitors.begin(), monitor);
    }

    void move_to_deactivated_list(Monitor* monitor)
    {
        Guard guard(m_all_monitors_lock);
        auto iter = std::find(m_all_monitors.begin(), m_all_monitors.end(), monitor);
        mxb_assert(iter != m_all_monitors.end());
        m_all_monitors.erase(iter);
        m_deact_monitors.push_back(monitor);
    }

private:
    std::mutex            m_all_monitors_lock;  /**< Protects access to arrays */
    std::vector<Monitor*> m_all_monitors;       /**< Global list of monitors, in configuration file order */
    std::vector<Monitor*> m_deact_monitors;     /**< Deactivated monitors. TODO: delete monitors */
};

ThisUnit this_unit;

const char RECONFIG_FAILED[] = "Monitor reconfiguration failed when %s. Check log for more details.";
}

Monitor* MonitorManager::create_monitor(const string& name, const string& module_name,
                                        mxs::ConfigParameters* params)
{
    mxb_assert(Monitor::is_main_worker());
    Monitor* new_monitor = nullptr;
    const MXS_MODULE* module = get_module(module_name, mxs::ModuleType::MONITOR);
    if (module)
    {
        if (module->specification && !module->specification->validate(*params))
        {
            return nullptr;
        }

        MXS_MONITOR_API* api = (MXS_MONITOR_API*)module->module_object;
        new_monitor = api->createInstance(name, module_name);
        if (new_monitor)
        {
            config_add_defaults(params, common_monitor_params());
            config_add_defaults(params, module->parameters);
            if (new_monitor->configure(params))
            {
                this_unit.insert_front(new_monitor);
            }
            else
            {
                delete new_monitor;
                new_monitor = nullptr;
            }
        }
        else
        {
            MXS_ERROR("Unable to create monitor instance for '%s', using module '%s'.",
                      name.c_str(), module_name.c_str());
        }
    }
    else
    {
        MXS_ERROR("Unable to load library file for monitor '%s'.", name.c_str());
    }
    return new_monitor;
}

bool MonitorManager::wait_one_tick()
{
    mxb_assert(Monitor::is_main_worker());
    std::map<Monitor*, long> tick_counts;

    // Get tick values for all monitors and instruct monitors to skip normal waiting.
    this_unit.foreach_monitor(
        [&tick_counts](Monitor* mon) {
            if (mon->is_running())
            {
                tick_counts[mon] = mon->ticks();
                mon->request_immediate_tick();
            }
            return true;
        });

    bool wait_success = true;
    auto wait_start = maxbase::Clock::now();
    // Due to immediate tick, monitors should generally run within 100ms. Slow-running operations on
    // backends may cause delay.
    auto time_limit = mxb::from_secs(10);

    auto sleep_time = std::chrono::milliseconds(30);
    std::this_thread::sleep_for(sleep_time);

    // Wait for all running monitors to advance at least one tick.
    this_unit.foreach_monitor(
        [&](Monitor* mon) {
            if (mon->is_running())
            {
                // Monitors may (in theory) have been modified between the two 'foreach_monitor'-calls.
                // Check if entry exists.
                auto it = tick_counts.find(mon);
                if (it != tick_counts.end())
                {
                    auto prev_tick_count = it->second;
                    while (true)
                    {
                        if (mon->ticks() != prev_tick_count)
                        {
                            break;
                        }
                        else if (maxbase::Clock::now() - wait_start > time_limit)
                        {
                            wait_success = false;
                            break;
                        }
                        else
                        {
                            // Not ideal to sleep while holding a mutex.
                            std::this_thread::sleep_for(sleep_time);
                        }
                    }
                }
            }
            return true;
        });

    return wait_success;
}

void MonitorManager::destroy_all_monitors()
{
    mxb_assert(Monitor::is_main_worker());
    auto monitors = this_unit.clear();
    for (auto monitor : monitors)
    {
        mxb_assert(!monitor->is_running());
        delete monitor;
    }
}

void MonitorManager::start_monitor(Monitor* monitor)
{
    mxb_assert(Monitor::is_main_worker());

    // Only start the monitor if it's stopped.
    if (!monitor->is_running())
    {
        if (!monitor->start())
        {
            MXS_ERROR("Failed to start monitor '%s'.", monitor->name());
        }
    }
}

void MonitorManager::populate_services()
{
    mxb_assert(Monitor::is_main_worker());
    this_unit.foreach_monitor(
        [](Monitor* pMonitor) -> bool {
            pMonitor->populate_services();
            return true;
        });
}

/**
 * Start all monitors
 */
void MonitorManager::start_all_monitors()
{
    mxb_assert(Monitor::is_main_worker());
    this_unit.foreach_monitor(
        [](Monitor* monitor) {
            MonitorManager::start_monitor(monitor);
            return true;
        });
}

void MonitorManager::stop_monitor(Monitor* monitor)
{
    mxb_assert(Monitor::is_main_worker());

    /** Only stop the monitor if it is running */
    if (monitor->is_running())
    {
        monitor->stop();
    }
}

void MonitorManager::deactivate_monitor(Monitor* monitor)
{
    mxb_assert(Monitor::is_main_worker());
    // This cannot be done with configure(), since other, module-specific config settings may depend on the
    // "servers"-setting of the base monitor.
    monitor->deactivate();
    this_unit.move_to_deactivated_list(monitor);
}

/**
 * Shutdown all running monitors
 */
void MonitorManager::stop_all_monitors()
{
    mxb_assert(Monitor::is_main_worker());
    this_unit.foreach_monitor(
        [](Monitor* monitor) {
            MonitorManager::stop_monitor(monitor);
            return true;
        });
}

/**
 * Find a monitor by name
 *
 * @param       name    The name of the monitor
 * @return      Pointer to the monitor or NULL
 */
Monitor* MonitorManager::find_monitor(const char* name)
{
    Monitor* rval = nullptr;
    this_unit.foreach_monitor(
        [&rval, name](Monitor* ptr) {
            if (ptr->m_name == name)
            {
                rval = ptr;
            }
            return rval == nullptr;
        });
    return rval;
}

Monitor* MonitorManager::server_is_monitored(const SERVER* server)
{
    Monitor* rval = nullptr;
    auto mon_name = Monitor::get_server_monitor(server);
    if (!mon_name.empty())
    {
        rval = find_monitor(mon_name.c_str());
        mxb_assert(rval);
    }
    return rval;
}

std::ostream& MonitorManager::monitor_persist(const Monitor* monitor, std::ostream& os)
{
    const MXS_MODULE* mod = get_module(monitor->m_module, mxs::ModuleType::MONITOR);
    mxb_assert(mod);

    os << generate_config_string(monitor->m_name, monitor->parameters(),
                                 common_monitor_params(), mod->parameters);

    return os;
}

bool MonitorManager::reconfigure_monitor(mxs::Monitor* monitor, const mxs::ConfigParameters& parameters)
{
    mxb_assert(Monitor::is_main_worker());
    // Backup monitor parameters in case configure fails.
    auto orig = monitor->parameters();
    // Stop/start monitor if it's currently running. If monitor was stopped already, this is likely
    // managed by the caller.
    bool stopstart = monitor->is_running();
    if (stopstart)
    {
        monitor->stop();
    }

    bool success = monitor->configure(&parameters);

    if (!success)
    {
        // Try to restore old values, it should work.
        MXB_AT_DEBUG(bool check = ) monitor->configure(&orig);
        mxb_assert(check);
    }

    if (stopstart && !monitor->start())
    {
        MXB_ERROR("Reconfiguration of monitor '%s' failed because monitor did not start.", monitor->name());
    }
    return success;
}

bool MonitorManager::alter_monitor(mxs::Monitor* monitor, const std::string& key, const std::string& value,
                                   std::string* error_out)
{
    const MXS_MODULE* mod = get_module(monitor->m_module, mxs::ModuleType::MONITOR);
    if (!validate_param(common_monitor_params(), mod->parameters, key, value, error_out))
    {
        return false;
    }

    mxs::ConfigParameters modified = monitor->parameters();
    modified.set(key, value);

    bool success = MonitorManager::reconfigure_monitor(monitor, modified);
    if (!success)
    {
        *error_out = string_printf(RECONFIG_FAILED, "changing a parameter");
    }
    return success;
}

json_t* MonitorManager::monitor_to_json(const Monitor* monitor, const char* host)
{
    string self = MXS_JSON_API_MONITORS;
    self += monitor->m_name;
    return mxs_json_resource(host, self.c_str(), monitor->to_json(host));
}

json_t* MonitorManager::monitored_server_attributes_json(const SERVER* srv)
{
    mxb_assert(Monitor::is_main_worker());
    Monitor* mon = server_is_monitored(srv);
    if (mon)
    {
        return mon->monitored_server_json_attributes(srv);
    }
    return nullptr;
}

json_t* MonitorManager::monitor_list_to_json(const char* host)
{
    json_t* rval = json_array();
    this_unit.foreach_monitor(
        [rval, host](Monitor* mon) {
            json_t* json = mon->to_json(host);
            if (json)
            {
                json_array_append_new(rval, json);
            }
            return true;
        });

    return mxs_json_resource(host, MXS_JSON_API_MONITORS, rval);
}

json_t* MonitorManager::monitor_relations_to_server(const SERVER* server,
                                                    const std::string& host,
                                                    const std::string& self)
{
    mxb_assert(Monitor::is_main_worker());
    json_t* rel = nullptr;

    string mon_name = Monitor::get_server_monitor(server);
    if (!mon_name.empty())
    {
        rel = mxs_json_relationship(host, self, MXS_JSON_API_MONITORS);
        mxs_json_add_relation(rel, mon_name.c_str(), CN_MONITORS);
    }

    return rel;
}

bool MonitorManager::set_server_status(SERVER* srv, int bit, string* errmsg_out)
{
    mxb_assert(Monitor::is_main_worker());
    bool written = false;
    Monitor* mon = MonitorManager::server_is_monitored(srv);
    if (mon)
    {
        written = mon->set_server_status(srv, bit, errmsg_out);
    }
    else
    {
        /* Set the bit directly */
        srv->set_status(bit);
        written = true;
    }
    return written;
}

bool MonitorManager::clear_server_status(SERVER* srv, int bit, string* errmsg_out)
{
    mxb_assert(Monitor::is_main_worker());
    bool written = false;
    Monitor* mon = MonitorManager::server_is_monitored(srv);
    if (mon)
    {
        written = mon->clear_server_status(srv, bit, errmsg_out);
    }
    else
    {
        /* Clear bit directly */
        srv->clear_status(bit);
        written = true;
    }
    return written;
}

bool MonitorManager::add_server_to_monitor(mxs::Monitor* mon, SERVER* server, std::string* error_out)
{
    mxb_assert(Monitor::is_main_worker());
    bool success = false;
    string server_monitor = Monitor::get_server_monitor(server);
    if (!server_monitor.empty())
    {
        // Error, server is already monitored.
        string error = string_printf("Server '%s' is already monitored by '%s', ",
                                     server->name(), server_monitor.c_str());
        error += (server_monitor == mon->name()) ? "cannot add again to the same monitor." :
            "cannot add to another monitor.";
        *error_out = error;
    }
    else
    {
        // To keep monitor modifications straightforward, all changes should go through the same
        // reconfigure-function. As the function accepts key-value combinations (so that they are easily
        // serialized), construct the value here.
        mxs::ConfigParameters modified_params = mon->parameters();
        string serverlist = modified_params.get_string(CN_SERVERS);
        if (serverlist.empty())
        {
            // Unusual.
            serverlist += server->name();
        }
        else
        {
            serverlist += string(", ") + server->name();
        }
        modified_params.set(CN_SERVERS, serverlist);
        success = reconfigure_monitor(mon, modified_params);
        if (!success)
        {
            *error_out = string_printf(RECONFIG_FAILED, "adding a server");
        }
    }
    return success;
}

bool MonitorManager::remove_server_from_monitor(mxs::Monitor* mon, SERVER* server, std::string* error_out)
{
    mxb_assert(Monitor::is_main_worker());
    bool success = false;
    string server_monitor = Monitor::get_server_monitor(server);
    if (server_monitor != mon->name())
    {
        // Error, server is not monitored by given monitor.
        string error;
        if (server_monitor.empty())
        {
            error = string_printf("Server '%s' is not monitored by any monitor, ", server->name());
        }
        else
        {
            error = string_printf("Server '%s' is monitored by '%s', ",
                                  server->name(), server_monitor.c_str());
        }
        error += string_printf("cannot remove it from '%s'.", mon->name());
        *error_out = error;
    }
    else
    {
        // Construct the new server list
        auto params = mon->parameters();
        auto names = config_break_list_string(params.get_string(CN_SERVERS));
        names.erase(std::remove(names.begin(), names.end(), server->name()));
        std::string servers = mxb::join(names, ",");
        params.set(CN_SERVERS, servers);
        success = MonitorManager::reconfigure_monitor(mon, params);
        if (!success)
        {
            *error_out = string_printf(RECONFIG_FAILED, "removing a server");
        }
    }

    return success;
}
