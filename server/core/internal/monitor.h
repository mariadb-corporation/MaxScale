#pragma once
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
 * @file core/maxscale/monitor.h - The private monitor interface
 */

#include <maxscale/monitor.h>
#include "externcmd.h"

MXS_BEGIN_DECLS

#define MON_ARG_MAX 8192

#define DEFAULT_CONNECT_TIMEOUT 3
#define DEFAULT_READ_TIMEOUT 1
#define DEFAULT_WRITE_TIMEOUT 2
#define DEFAULT_CONNECTION_ATTEMPTS 1

#define DEFAULT_MONITOR_INTERVAL 2000 // in milliseconds

/** Default maximum journal age in seconds */
#define DEFAULT_JOURNAL_MAX_AGE 28800

/** Default script execution timeout in seconds */
#define DEFAULT_SCRIPT_TIMEOUT 90

/**
 * Monitor network timeout types
 */
typedef enum
{
    MONITOR_CONNECT_TIMEOUT = 0,
    MONITOR_READ_TIMEOUT    = 1,
    MONITOR_WRITE_TIMEOUT   = 2,
    MONITOR_CONNECT_ATTEMPTS = 3
} monitor_timeouts_t;

MXS_MONITOR *monitor_create(const char *, const char *, MXS_CONFIG_PARAMETER* params);
void monitor_destroy(MXS_MONITOR *);

void monitor_start(MXS_MONITOR *, const MXS_CONFIG_PARAMETER*);
void monitor_stop(MXS_MONITOR *);

/**
 * @brief Mark monitor as deactivated
 *
 * A deactivated monitor appears not to exist, as if it had been
 * destroyed.
 *
 * @param monitor
 */
void monitor_deactivate(MXS_MONITOR* monitor);

void monitor_stop_all();
void monitor_start_all();

/**
 * @brief Destroys all monitors. At this point all monitors should
 *        have been stopped.
 *
 * @attn Must only be called in single-thread context at system shutdown.
 */
void monitor_destroy_all();

MXS_MONITOR *monitor_find(const char *);
MXS_MONITOR* monitor_repurpose_destroyed(const char* name, const char* module);

void monitor_show(DCB *, MXS_MONITOR *);
void monitor_show_all(DCB *);

void monitor_list(DCB *);

bool monitor_add_server(MXS_MONITOR *mon, SERVER *server);
void monitor_remove_server(MXS_MONITOR *mon, SERVER *server);
void monitor_add_user(MXS_MONITOR *, const char *, const char *);
void monitor_add_parameters(MXS_MONITOR *monitor, MXS_CONFIG_PARAMETER *params);
bool monitor_remove_parameter(MXS_MONITOR *monitor, const char *key);

void monitor_set_interval (MXS_MONITOR *, unsigned long);
bool monitor_set_network_timeout(MXS_MONITOR *, int, int, const char*);
void monitor_set_journal_max_age(MXS_MONITOR *mon, time_t value);
void monitor_set_script_timeout(MXS_MONITOR *mon, uint32_t value);

/**
 * @brief Serialize a monitor to a file
 *
 * This converts the static configuration of the monitor into an INI format file.
 *
 * @param monitor Monitor to serialize
 * @return True if serialization was successful
 */
bool monitor_serialize(const MXS_MONITOR *monitor);

/**
 * Check if a server is being monitored and return the monitor.
 * @param server Server that is queried
 * @return The monitor watching this server, or NULL if not monitored
 */
MXS_MONITOR* monitor_server_in_use(const SERVER *server);

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
int monitor_launch_script(MXS_MONITOR* mon, MXS_MONITORED_SERVER* ptr, const char* script, uint32_t timeout);

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
int monitor_launch_command(MXS_MONITOR* mon, MXS_MONITORED_SERVER* ptr, EXTERNCMD* cmd);

MXS_END_DECLS
