#ifndef _MONITOR_H
#define _MONITOR_H
/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */
#include <mysql.h>
#include <server.h>
#include <dcb.h>
#include <log_manager.h>
#include <resultset.h>
#include <maxconfig.h>
#include <externcmd.h>
#include <secrets.h>

/**
 * @file monitor.h      The interface to the monitor module
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 07/07/13     Mark Riddoch            Initial implementation
 * 25/07/13     Mark Riddoch            Addition of diagnotics
 * 23/05/14     Mark Riddoch            Addition of routine to find monitors by name
 * 23/05/14     Massimiliano Pinto      Addition of defaultId and setInterval
 * 23/06/14     Massimiliano Pinto      Addition of replicationHeartbeat
 * 28/08/14     Massimiliano Pinto      Addition of detectStaleMaster
 * 30/10/14     Massimiliano Pinto      Addition of disableMasterFailback
 * 07/11/14     Massimiliano Pinto      Addition of setNetworkTimeout
 * 19/02/15     Mark Riddoch            Addition of monitorGetList
 * 19/11/15     Martin Brampton         Automation of event and name declaration, absorption
 *                                      of what was formerly monitor_common.h
 *
 * @endverbatim
 */

/**
 * The "Module Object" for a monitor module.
 *
 * The monitor modules are designed to monitor the backend databases that the gateway
 * connects to and provide information regarding the status of the databases that
 * is used in the routing decisions.
 *
 * startMonitor is called to start the monitoring process, it is called on the main
 * thread of the gateway and is responsible for creating a thread for the monitor
 * itself to run on. This should use the entry points defined in the thread.h
 * header file rather than make direct calls to the operating system thrading libraries.
 * The return from startMonitor is a void * handle that will be passed to all other monitor
 * API calls.
 *
 * stopMonitor is responsible for shuting down and destroying a monitor, it is called
 * with the void * handle that was returned by startMonitor.
 *
 * registerServer is called to register a server that must be monitored with a running
 * monitor. this will be called with the handle returned from the startMonitor call and
 * the SERVER structure that the monitor must update and monitor. The SERVER structure
 * contains the information required to connect to the monitored server.
 *
 * unregisterServer is called to remove a server from the set of servers that need to be
 * monitored.
 */
typedef struct
{
    void *(*startMonitor)(void *, void*);
    void (*stopMonitor)(void *);
    void (*diagnostics)(DCB *, void *);
} MONITOR_OBJECT;

/**
 * The monitor API version number. Any change to the monitor module API
 * must change these versions usign the rules defined in modinfo.h
 */
#define MONITOR_VERSION {3, 0, 0}

/** Monitor's poll frequency */
#define MON_BASE_INTERVAL_MS 100

/**
 * Monitor state bit mask values
 */
typedef enum
{
    MONITOR_STATE_ALLOC     = 0x00,
    MONITOR_STATE_RUNNING   = 0x01,
    MONITOR_STATE_STOPPING  = 0x02,
    MONITOR_STATE_STOPPED   = 0x04,
    MONITOR_STATE_FREED     = 0x08
} monitor_state_t;

/**
 * Monitor network timeout types
 */
typedef enum
{
    MONITOR_CONNECT_TIMEOUT = 0,
    MONITOR_READ_TIMEOUT    = 1,
    MONITOR_WRITE_TIMEOUT   = 2
} monitor_timeouts_t;

/*
 * Results of attempt at database connection for monitoring
 */
typedef enum
{
    MONITOR_CONN_OK,
    MONITOR_CONN_REFUSED,
    MONITOR_CONN_TIMEOUT
} connect_result_t;

#define MON_ARG_MAX 8192

#define DEFAULT_CONNECT_TIMEOUT 3
#define DEFAULT_READ_TIMEOUT 1
#define DEFAULT_WRITE_TIMEOUT 2


#define MONITOR_RUNNING 1
#define MONITOR_STOPPING 2
#define MONITOR_STOPPED 3

#define MONITOR_INTERVAL 10000 // in milliseconds
#define MONITOR_DEFAULT_ID 1UL // unsigned long value
#define MONITOR_MAX_NUM_SLAVES 20 //number of MySQL slave servers associated to a MySQL master server

/*
 * Create declarations of the enum for monitor events and also the array of
 * structs containing the matching names. The data is taken from def_monitor_event.h
 */
#undef  ADDITEM
#define ADDITEM( _event_type, _event_name )      _event_type
typedef enum
{
#include "def_monitor_event.h"
    MAX_MONITOR_EVENT
} monitor_event_t;
#undef  ADDITEM

typedef struct monitor_def_s
{
    char name[30];
} monitor_def_t;

extern const monitor_def_t monitor_event_definitions[];

/**
 * The linked list of servers that are being monitored by the monitor module.
 */
typedef struct monitor_servers
{
    SERVER *server;               /**< The server being monitored */
    MYSQL *con;                   /**< The MySQL connection */
    bool log_version_err;
    int mon_err_count;
    unsigned int mon_prev_status;
    unsigned int pending_status;  /**< Pending Status flag bitmap */
    struct monitor_servers *next; /**< The next server in the list */
} MONITOR_SERVERS;

/**
 * Representation of the running monitor.
 */
typedef struct monitor
{
    char *name;                   /**< The name of the monitor module */
    char *user;                   /*< Monitor username */
    char *password;               /*< Monitor password */
    SPINLOCK lock;
    CONFIG_PARAMETER* parameters; /*< configuration parameters */
    MONITOR_SERVERS* databases;   /*< List of databases the monitor monitors */
    monitor_state_t state;        /**< The state of the monitor */
    int connect_timeout;          /**< Connect timeout in seconds for mysql_real_connect */
    int read_timeout;             /**< Timeout in seconds to read from the server.
                                   * There are retries and the total effective timeout
                                   * value is three times the option value.
                                   */
    int write_timeout;            /**< Timeout in seconds for each attempt to write to the server.
                                     * There are retries and the total effective timeout value is
                                     * two times the option value.
                                     */
    MONITOR_OBJECT *module;       /**< The "monitor object" */
    void *handle;                 /**< Handle returned from startMonitor */
    size_t interval;              /**< The monitor interval */
    struct monitor *next;         /**< Next monitor in the linked list */
} MONITOR;

extern MONITOR *monitor_alloc(char *, char *);
extern void monitor_free(MONITOR *);
extern MONITOR *monitor_find(char *);
extern void monitorAddServer(MONITOR *, SERVER *);
extern void monitorAddUser(MONITOR *, char *, char *);
extern void monitorAddParameters(MONITOR *monitor, CONFIG_PARAMETER *params);
extern void monitorStop(MONITOR *);
extern void monitorStart(MONITOR *, void*);
extern void monitorStopAll();
extern void monitorStartAll();
extern void monitorShowAll(DCB *);
extern void monitorShow(DCB *, MONITOR *);
extern void monitorList(DCB *);
extern void monitorSetInterval (MONITOR *, unsigned long);
extern void monitorSetNetworkTimeout(MONITOR *, int, int);
extern RESULTSET *monitorGetList();
bool check_monitor_permissions(MONITOR* monitor);

monitor_event_t mon_name_to_event(const char* tok);
void mon_append_node_names(MONITOR_SERVERS* start, char* str, int len);
monitor_event_t mon_get_event_type(MONITOR_SERVERS* node);
const char* mon_get_event_name(MONITOR_SERVERS* node);
void monitor_clear_pending_status(MONITOR_SERVERS *ptr, int bit);
void monitor_set_pending_status(MONITOR_SERVERS *ptr, int bit);
bool mon_status_changed(MONITOR_SERVERS* mon_srv);
bool mon_print_fail_status(MONITOR_SERVERS* mon_srv);
void monitor_launch_script(MONITOR* mon, MONITOR_SERVERS* ptr, char* script);
int mon_parse_event_string(bool* events, size_t count, char* string);
connect_result_t mon_connect_to_db(MONITOR* mon, MONITOR_SERVERS *database);
void mon_log_connect_error(MONITOR_SERVERS* database, connect_result_t rval);

#endif
