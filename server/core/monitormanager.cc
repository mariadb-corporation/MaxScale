/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>

#include <maxscale/json_api.hh>
#include <maxscale/paths.h>

#include "internal/config.hh"
#include "internal/monitor.hh"
#include "internal/modules.hh"

using maxscale::Monitor;
using maxscale::MonitorServer;
using Guard = std::lock_guard<std::mutex>;
using std::string;

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
    std::mutex m_all_monitors_lock;          /**< Protects access to arrays */
    std::vector<Monitor*> m_all_monitors;    /**< Global list of monitors, in configuration file order */
    std::vector<Monitor*> m_deact_monitors;  /**< Deactivated monitors. TODO: delete monitors */
};

ThisUnit this_unit;

}

Monitor* MonitorManager::create_monitor(const string& name, const string& module,
                                        MXS_CONFIG_PARAMETER* params)
{
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
    using namespace std::chrono;
    std::map<Monitor*, uint64_t> ticks;

    // Get tick values for all monitors
    this_unit.foreach_monitor([&ticks](Monitor* mon) {
        ticks[mon] = mxb::atomic::load(&mon->m_ticks);
        return true;
    });

    // Wait for all running monitors to advance at least one tick.
    this_unit.foreach_monitor([&ticks](Monitor* mon) {
        if (mon->state() == MONITOR_STATE_RUNNING)
        {
            auto start = steady_clock::now();
            // A monitor may have been added in between the two foreach-calls (not if config changes are
            // serialized). Check if entry exists.
            if (ticks.count(mon) > 0)
            {
                auto tick = ticks[mon];
                while (mxb::atomic::load(&mon->m_ticks) == tick && (steady_clock::now() - start < seconds(60)))
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
    auto monitors = this_unit.clear();
    for (auto monitor : monitors)
    {
        mxb_assert(monitor->state() == MONITOR_STATE_STOPPED);
        delete monitor;
    }
}

void MonitorManager::start_monitor(Monitor* monitor)
{
    mxb_assert(monitor);

    Guard guard(monitor->m_lock);

    // Only start the monitor if it's stopped.
    if (monitor->state() == MONITOR_STATE_STOPPED)
    {
        if (!monitor->start())
        {
            MXS_ERROR("Failed to start monitor '%s'.", monitor->name());
        }
    }
}

void MonitorManager::populate_services()
{
    this_unit.foreach_monitor([](Monitor* pMonitor) -> bool {
        pMonitor->populate_services();
        return true;
    });
}

/**
 * Start all monitors
 */
void MonitorManager::start_all_monitors()
{
    this_unit.foreach_monitor([](Monitor* monitor) {
        MonitorManager::start_monitor(monitor);
        return true;
    });
}

void MonitorManager::stop_monitor(Monitor* monitor)
{
    mxb_assert(monitor);

    Guard guard(monitor->m_lock);

    /** Only stop the monitor if it is running */
    if (monitor->state() == MONITOR_STATE_RUNNING)
    {
        monitor->stop();
    }
}

void MonitorManager::deactivate_monitor(Monitor* monitor)
{
    // This cannot be done with configure(), since other, module-specific config settings may depend on the
    // "servers"-setting of the base monitor. Directly manipulate monitor field for now, later use a dtor
    // to cleanly "deactivate" inherited objects.
    stop_monitor(monitor);
    while (!monitor->m_servers.empty())
    {
        monitor->remove_server(monitor->m_servers.front()->server);
    }
    this_unit.move_to_deactivated_list(monitor);
}

/**
 * Shutdown all running monitors
 */
void MonitorManager::stop_all_monitors()
{
    this_unit.foreach_monitor([](Monitor* monitor) {
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
    this_unit.foreach_monitor([dcb](Monitor* monitor) {
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
    monitor->show(dcb);
}

/**
 * List all the monitors
 *
 * @param dcb   DCB for printing output
 */
void MonitorManager::monitor_list(DCB* dcb)
{
    dcb_printf(dcb, "---------------------+---------------------\n");
    dcb_printf(dcb, "%-20s | Status\n", "Monitor");
    dcb_printf(dcb, "---------------------+---------------------\n");

    this_unit.foreach_monitor([dcb](Monitor* ptr) {
        dcb_printf(dcb, "%-20s | %s\n",
                   ptr->name(), ptr->state() == MONITOR_STATE_RUNNING ? "Running" : "Stopped");
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
    this_unit.foreach_monitor([&rval, name](Monitor* ptr) {
        if (ptr->m_name == name)
        {
            rval = ptr;
        }
        return (rval == nullptr);
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
    std::unique_ptr<ResultSet> set = ResultSet::create({"Monitor", "Status"});
    this_unit.foreach_monitor([&set](Monitor* ptr) {
        const char* state = ptr->state() == MONITOR_STATE_RUNNING ? "Running" : "Stopped";
        set->add_row({ptr->m_name, state});
        return true;
    });
    return set;
}

Monitor* MonitorManager::server_is_monitored(const SERVER* server)
{
    Monitor* rval = nullptr;
    this_unit.foreach_monitor([&rval, server](Monitor* monitor) {
        Guard guard(monitor->m_lock);
        for (MonitorServer* db : monitor->m_servers)
        {
            if (db->server == server)
            {
                rval = monitor;
                break;
            }
        }
        return (rval == nullptr);
    });
    return rval;
}

bool MonitorManager::create_monitor_config(const Monitor* monitor, const char* filename)
{
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

    {
        Guard guard(monitor->m_lock);

        const MXS_MODULE* mod = get_module(monitor->m_module.c_str(), NULL);
        mxb_assert(mod);

        string config = generate_config_string(monitor->m_name, monitor->parameters,
                                               config_monitor_params, mod->parameters);

        if (dprintf(file, "%s", config.c_str()) == -1)
        {
                    MXS_ERROR("Could not write serialized configuration to file '%s': %d, %s",
                              filename, errno, mxs_strerror(errno));
        }
    }

    close(file);
    return true;
}

bool MonitorManager::monitor_serialize(const Monitor* monitor)
{
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

// static
bool MonitorManager::reconfigure_monitor(mxs::Monitor* monitor, const MXS_CONFIG_PARAMETER& parameters)
{
    // Backup monitor parameters in case configure fails.
    auto orig = monitor->parameters;
    monitor->parameters.clear();

    bool success = monitor->configure(&parameters);

    if (!success)
    {
        MXB_AT_DEBUG(bool check = ) monitor->configure(&orig);
        mxb_assert(check);
    }

    return success;
}

json_t* MonitorManager::monitor_to_json(const Monitor* monitor, const char* host)
{
    string self = MXS_JSON_API_MONITORS;
    self += monitor->m_name;
    return mxs_json_resource(host, self.c_str(), monitor_json_data(monitor, host));
}

json_t* MonitorManager::monitor_list_to_json(const char* host)
{
    json_t* rval = json_array();
    this_unit.foreach_monitor([rval, host](Monitor* mon) {
        json_t* json = monitor_json_data(mon, host);
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
    std::vector<std::string> names;
    this_unit.foreach_monitor([&names, server](Monitor* mon) {
        Guard guard(mon->m_lock);
        for (MonitorServer* db : mon->m_servers)
        {
            if (db->server == server)
            {
                names.push_back(mon->m_name);
                break;
            }
        }
        return true;
    });

    json_t* rel = NULL;
    if (!names.empty())
    {
        rel = mxs_json_relationship(host, MXS_JSON_API_MONITORS);

        for (std::vector<std::string>::iterator it = names.begin();
             it != names.end(); it++)
        {
            mxs_json_add_relation(rel, it->c_str(), CN_MONITORS);
        }
    }

    return rel;
}
