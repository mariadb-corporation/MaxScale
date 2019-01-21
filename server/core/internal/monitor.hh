/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

/**
 * Internal header for the monitor
 */

#include <maxscale/monitor.hh>
#include <maxscale/resultset.hh>
#include "externcmd.hh"

#define MON_ARG_MAX 8192

#define DEFAULT_CONNECT_TIMEOUT     3
#define DEFAULT_READ_TIMEOUT        3
#define DEFAULT_WRITE_TIMEOUT       3
#define DEFAULT_CONNECTION_ATTEMPTS 1

#define DEFAULT_MONITOR_INTERVAL 2000   // in milliseconds

/** Default maximum journal age in seconds */
#define DEFAULT_JOURNAL_MAX_AGE 28800

/** Default script execution timeout in seconds */
#define DEFAULT_SCRIPT_TIMEOUT 90

/**
 * Monitor network timeout types
 */
typedef enum
{
    MONITOR_CONNECT_TIMEOUT  = 0,
    MONITOR_READ_TIMEOUT     = 1,
    MONITOR_WRITE_TIMEOUT    = 2,
    MONITOR_CONNECT_ATTEMPTS = 3
} monitor_timeouts_t;

/* Is not really an event as the other values, but is a valid config setting and also the default.
 * Bitmask value matches all events. */
static const MXS_ENUM_VALUE mxs_monitor_event_default_enum = {"all", ~0ULL};
static const MXS_ENUM_VALUE mxs_monitor_event_enum_values[] =
{
    mxs_monitor_event_default_enum,
    {"master_down",                MASTER_DOWN_EVENT },
    {"master_up",                  MASTER_UP_EVENT   },
    {"slave_down",                 SLAVE_DOWN_EVENT  },
    {"slave_up",                   SLAVE_UP_EVENT    },
    {"server_down",                SERVER_DOWN_EVENT },
    {"server_up",                  SERVER_UP_EVENT   },
    {"synced_down",                SYNCED_DOWN_EVENT },
    {"synced_up",                  SYNCED_UP_EVENT   },
    {"donor_down",                 DONOR_DOWN_EVENT  },
    {"donor_up",                   DONOR_UP_EVENT    },
    {"ndb_down",                   NDB_DOWN_EVENT    },
    {"ndb_up",                     NDB_UP_EVENT      },
    {"lost_master",                LOST_MASTER_EVENT },
    {"lost_slave",                 LOST_SLAVE_EVENT  },
    {"lost_synced",                LOST_SYNCED_EVENT },
    {"lost_donor",                 LOST_DONOR_EVENT  },
    {"lost_ndb",                   LOST_NDB_EVENT    },
    {"new_master",                 NEW_MASTER_EVENT  },
    {"new_slave",                  NEW_SLAVE_EVENT   },
    {"new_synced",                 NEW_SYNCED_EVENT  },
    {"new_donor",                  NEW_DONOR_EVENT   },
    {"new_ndb",                    NEW_NDB_EVENT     },
    {NULL}
};

std::unique_ptr<ResultSet> monitor_get_list();

/**
 * This class contains internal monitor management functions that should not be exposed in the public
 * monitor class. It's a friend of MXS_MONITOR.
 */
class MonitorManager
{
public:

    /**
     * Creates a new monitor. Loads the module, calls constructor and configure, and adds monitor to the
     * global list.
     *
     * @param name          The configuration name of the monitor
     * @param module        The module name to load
     * @return              The newly created monitor, or NULL on error
     */
    static Monitor* create_monitor(const std::string& name, const std::string& module,
                                   MXS_CONFIG_PARAMETER* params);

    /**
     * @brief Destroys all monitors. At this point all monitors should
     *        have been stopped.
     *
     * @attn Must only be called in single-thread context at system shutdown.
     */
    static void destroy_all_monitors();

    /**
     * Free a monitor, first stop the monitor and then remove the monitor from
     * the chain of monitors and free the memory.
     *
     * @param mon   The monitor to free
     */
    static void destroy_monitor(Monitor*);
};


void monitor_start(Monitor*, const MXS_CONFIG_PARAMETER*);
void monitor_stop(Monitor*);

/**
 * @brief Mark monitor as deactivated
 *
 * A deactivated monitor appears not to exist, as if it had been
 * destroyed.
 *
 * @param monitor
 */
void monitor_deactivate(Monitor* monitor);

void monitor_stop_all();
void monitor_start_all();

Monitor* monitor_find(const char*);
Monitor* monitor_repurpose_destroyed(const char* name, const char* module);

void monitor_show(DCB*, Monitor*);
void monitor_show_all(DCB*);

void monitor_list(DCB*);

bool monitor_add_server(Monitor* mon, SERVER* server);
void monitor_remove_server(Monitor* mon, SERVER* server);
void monitor_add_user(Monitor*, const char*, const char*);
void monitor_add_parameters(Monitor* monitor, const MXS_CONFIG_PARAMETER* params);
bool monitor_remove_parameter(Monitor* monitor, const char* key);
void monitor_set_parameter(Monitor* monitor, const char* key, const char* value);

void monitor_set_interval(Monitor*, unsigned long);
bool monitor_set_network_timeout(Monitor*, int, int, const char*);
void monitor_set_journal_max_age(Monitor* mon, time_t value);
void monitor_set_script_timeout(Monitor* mon, uint32_t value);

/**
 * @brief Serialize a monitor to a file
 *
 * This converts the static configuration of the monitor into an INI format file.
 *
 * @param monitor Monitor to serialize
 * @return True if serialization was successful
 */
bool monitor_serialize(const Monitor* monitor);

/**
 * Check if a server is being monitored and return the monitor.
 * @param server Server that is queried
 * @return The monitor watching this server, or NULL if not monitored
 */
Monitor* monitor_server_in_use(const SERVER* server);

/**
 * Launch a script
 *
 * @param mon     Owning monitor
 * @param ptr     The server which has changed state
 * @param script  Script to execute
 * @param timeout Timeout in seconds for the script
 *
 * @return Return value of the executed script or -1 on error
 */
int monitor_launch_script(Monitor* mon, MXS_MONITORED_SERVER* ptr, const char* script, uint32_t timeout);

/**
 * Launch a command
 *
 * @param mon  Owning monitor
 * @param ptr  The server which has changed state
 * @param cmd  The command to execute.
 *
 * @note All default script variables will be replaced.
 *
 * @return Return value of the executed script or -1 on error.
 */
int monitor_launch_command(Monitor* mon, MXS_MONITORED_SERVER* ptr, EXTERNCMD* cmd);
