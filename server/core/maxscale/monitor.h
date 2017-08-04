#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file core/maxscale/monitor.h - The private monitor interface
 */

#include <maxscale/monitor.h>

MXS_BEGIN_DECLS

#define MON_ARG_MAX 8192

#define DEFAULT_CONNECT_TIMEOUT 3
#define DEFAULT_READ_TIMEOUT 1
#define DEFAULT_WRITE_TIMEOUT 2
#define DEFAULT_CONNECTION_ATTEMPTS 1

#define MONITOR_DEFAULT_INTERVAL 2000 // in milliseconds

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

MXS_MONITOR *monitor_alloc(const char *, const char *);
void monitor_free(MXS_MONITOR *);

void monitorStart(MXS_MONITOR *, const MXS_CONFIG_PARAMETER*);
void monitorStop(MXS_MONITOR *);
void monitorDestroy(MXS_MONITOR* monitor);
void monitorStopAll();
void monitorStartAll();

MXS_MONITOR *monitor_find(const char *);
MXS_MONITOR* monitor_repurpose_destroyed(const char* name, const char* module);

void monitorShow(DCB *, MXS_MONITOR *);
void monitorShowAll(DCB *);

void monitorList(DCB *);
RESULTSET *monitorGetList();

bool monitorAddServer(MXS_MONITOR *mon, SERVER *server);
void monitorRemoveServer(MXS_MONITOR *mon, SERVER *server);
void monitorAddUser(MXS_MONITOR *, const char *, const char *);
void monitorAddParameters(MXS_MONITOR *monitor, MXS_CONFIG_PARAMETER *params);
bool monitorRemoveParameter(MXS_MONITOR *monitor, const char *key);

void monitorSetInterval (MXS_MONITOR *, unsigned long);
bool monitorSetNetworkTimeout(MXS_MONITOR *, int, int);

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

MXS_END_DECLS
