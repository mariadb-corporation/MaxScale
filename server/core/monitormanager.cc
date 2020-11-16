/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>

#include <maxbase/format.hh>
#include <maxscale/json_api.hh>
#include <maxscale/paths.h>
#include <maxscale/resultset.hh>

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
    void foreach_monitor(std::function<bool(Monitor*)> apply)
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

Monitor* MonitorManager::create_monitor(const string& name, const string& module,
                                        MXS_CONFIG_PARAMETER* params)
{
    mxb_assert(Monitor::is_admin_thread());
    MXS_MONITOR_API* api = (MXS_MONITOR_API*)load_module(module.c_str(), MODULE_MONITOR);
    if (!api)
    {
        MXS_ERROR("Unable to load library file for monitor '%s'.", name.c_str());
        return NULL;
    }

    Monitor* mon = api->createInstance(name, module);
    if (!mon)
    {
        MXS_ERROR("Unable to create monitor instance for '%s', using module '%s'.",
                  name.c_str(), module.c_str());
        return NULL;
    }

    if (mon->configure(params))
    {
        this_unit.insert_front(mon);
    }
    else
    {
        delete mon;
        mon = NULL;
    }
    return mon;
}

void MonitorManager::debug_wait_one_tick()
{
    mxb_assert(Monitor::is_admin_thread());
    using namespace std::chrono;
    std::map<Monitor*, long> ticks;

    // Get tick values for all monitors
    this_unit.foreach_monitor(
        [&ticks](Monitor* mon) {
            ticks[mon] = mon->ticks();
            return true;
        });

    // Wait for all running monitors to advance at least one tick.
    this_unit.foreach_monitor(
        [&ticks](Monitor* mon) {
            if (mon->is_running())
            {
                auto start = steady_clock::now();
                // A monitor may have been added in between the two foreach-calls (not
                // if config changes are
                // serialized). Check if entry exists.
                if (ticks.count(mon) > 0)
                {
                    auto tick = ticks[mon];
                    while (mon->ticks() == tick
                           && (steady_clock::now() - start < seconds(60)))
                    {
                        std::this_thread::sleep_for(milliseconds(100));
                    }
                }
            }
            return true;
        });
}

void MonitorManager::destroy_all_monitors()
{
    mxb_assert(Monitor::is_admin_thread());
    auto monitors = this_unit.clear();
    for (auto monitor : monitors)
    {
        mxb_assert(!monitor->is_running());
        delete monitor;
    }
}

void MonitorManager::start_monitor(Monitor* monitor)
{
    mxb_assert(Monitor::is_admin_thread());

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
    mxb_assert(Monitor::is_admin_thread());
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
    mxb_assert(Monitor::is_admin_thread());
    this_unit.foreach_monitor(
        [](Monitor* monitor) {
            MonitorManager::start_monitor(monitor);
            return true;
        });
}

void MonitorManager::stop_monitor(Monitor* monitor)
{
    mxb_assert(Monitor::is_admin_thread());

    /** Only stop the monitor if it is running */
    if (monitor->is_running())
    {
        monitor->stop();
    }
}

void MonitorManager::deactivate_monitor(Monitor* monitor)
{
    mxb_assert(Monitor::is_admin_thread());
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
    mxb_assert(Monitor::is_admin_thread());
    this_unit.foreach_monitor(
        [](Monitor* monitor) {
            MonitorManager::stop_monitor(monitor);
            return true;
        });
}

/**
 * Show all monitors
 *
 * @param dcb   DCB for printing output
 */
void MonitorManager::show_all_monitors(DCB* dcb)
{
    mxb_assert(Monitor::is_admin_thread());
    this_unit.foreach_monitor(
        [dcb](Monitor* monitor) {
            monitor_show(dcb, monitor);
            return true;
        });
}

/**
 * Show a single monitor
 *
 * @param dcb   DCB for printing output
 */
void MonitorManager::monitor_show(DCB* dcb, Monitor* monitor)
{
    mxb_assert(Monitor::is_admin_thread());
    monitor->show(dcb);
}

/**
 * List all the monitors
 *
 * @param dcb   DCB for printing output
 */
void MonitorManager::monitor_list(DCB* dcb)
{
    mxb_assert(Monitor::is_admin_thread());
    dcb_printf(dcb, "---------------------+---------------------\n");
    dcb_printf(dcb, "%-20s | Status\n", "Monitor");
    dcb_printf(dcb, "---------------------+---------------------\n");

    this_unit.foreach_monitor(
        [dcb](Monitor* ptr) {
            dcb_printf(dcb, "%-20s | %s\n", ptr->name(), ptr->state_string());
            return true;
        });

    dcb_printf(dcb, "---------------------+---------------------\n");
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

/**
 * Return a resultset that has the current set of monitors in it
 *
 * @return A Result set
 */
std::unique_ptr<ResultSet> MonitorManager::monitor_get_list()
{
    mxb_assert(Monitor::is_admin_thread());
    std::unique_ptr<ResultSet> set = ResultSet::create({"Monitor", "Status"});
    this_unit.foreach_monitor(
        [&set](Monitor* ptr) {
            set->add_row({ptr->m_name, ptr->state_string()});
            return true;
        });
    return set;
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

bool MonitorManager::create_monitor_config(const Monitor* monitor, const char* filename)
{
    mxb_assert(Monitor::is_admin_thread());
    int file = open(filename, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (file == -1)
    {
        MXS_ERROR("Failed to open file '%s' when serializing monitor '%s': %d, %s",
                  filename,
                  monitor->name(),
                  errno,
                  mxs_strerror(errno));
        return false;
    }

    const MXS_MODULE* mod = get_module(monitor->m_module.c_str(), NULL);
    mxb_assert(mod);

    string config = generate_config_string(monitor->m_name, monitor->parameters(),
                                           config_monitor_params, mod->parameters);

    if (dprintf(file, "%s", config.c_str()) == -1)
    {
        MXS_ERROR("Could not write serialized configuration to file '%s': %d, %s",
                  filename, errno, mxs_strerror(errno));
    }

    close(file);
    return true;
}

bool MonitorManager::monitor_serialize(const Monitor* monitor)
{
    mxb_assert(Monitor::is_admin_thread());
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename,
             sizeof(filename),
             "%s/%s.cnf.tmp",
             get_config_persistdir(),
             monitor->name());

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary monitor configuration at '%s': %d, %s",
                  filename,
                  errno,
                  mxs_strerror(errno));
    }
    else if (create_monitor_config(monitor, filename))
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
            MXS_ERROR("Failed to rename temporary monitor configuration at '%s': %d, %s",
                      filename,
                      errno,
                      mxs_strerror(errno));
        }
    }

    return rval;
}

bool MonitorManager::reconfigure_monitor(mxs::Monitor* monitor, const MXS_CONFIG_PARAMETER& parameters)
{
    mxb_assert(Monitor::is_admin_thread());
    // Backup monitor parameters in case configure fails.
    auto orig = monitor->parameters();
    // Stop/start monitor if it's currently running. If monitor was stopped already, this is likely
    // managed by the caller.
    bool stopstart = monitor->is_running();
    if (stopstart)
    {
        monitor->stop();
    }

    bool success = false;
    if (monitor->configure(&parameters))
    {
        // Serialization must also succeed.
        success = MonitorManager::monitor_serialize(monitor);
    }

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
    const MXS_MODULE* mod = get_module(monitor->m_module.c_str(), MODULE_MONITOR);
    if (!validate_param(config_monitor_params, mod->parameters, key, value, error_out))
    {
        return false;
    }

    MXS_CONFIG_PARAMETER modified = monitor->parameters();
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

json_t* MonitorManager::monitor_relations_to_server(const SERVER* server, const char* host)
{
    mxb_assert(Monitor::is_admin_thread());
    json_t* rel = nullptr;

    string mon_name = Monitor::get_server_monitor(server);
    if (!mon_name.empty())
    {
        rel = mxs_json_relationship(host, MXS_JSON_API_MONITORS);
        mxs_json_add_relation(rel, mon_name.c_str(), CN_MONITORS);
    }

    return rel;
}

bool MonitorManager::set_server_status(SERVER* srv, int bit, string* errmsg_out)
{
    mxb_assert(Monitor::is_admin_thread());
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
    mxb_assert(Monitor::is_admin_thread());
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
    mxb_assert(Monitor::is_admin_thread());
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
        MXS_CONFIG_PARAMETER modified_params = mon->parameters();
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
    mxb_assert(Monitor::is_admin_thread());
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
