#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

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

#include <maxscale/cdefs.h>
#include <mysql.h>
#include <maxscale/server.h>
#include <maxscale/dcb.h>
#include <maxscale/log_manager.h>
#include <maxscale/resultset.h>
#include <maxscale/config.h>
#include <maxscale/externcmd.h>
#include <maxscale/secrets.h>
#include <maxscale/modinfo.h>

MXS_BEGIN_DECLS

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
 */

struct monitor;
typedef struct monitor MONITOR;

typedef struct
{
    void *(*startMonitor)(MONITOR *monitor, const CONFIG_PARAMETER *params);
    void (*stopMonitor)(MONITOR *monitor);
    void (*diagnostics)(DCB *, const MONITOR *);
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

#define MAX_MONITOR_USER_LEN     512
#define MAX_MONITOR_PASSWORD_LEN 512

/** Monitor events */
typedef enum
{
    UNDEFINED_EVENT   = 0,
    MASTER_DOWN_EVENT = (1 << 0),  /**< master_down */
    MASTER_UP_EVENT   = (1 << 1),  /**< master_up */
    SLAVE_DOWN_EVENT  = (1 << 2),  /**< slave_down */
    SLAVE_UP_EVENT    = (1 << 3),  /**< slave_up */
    SERVER_DOWN_EVENT = (1 << 4),  /**< server_down */
    SERVER_UP_EVENT   = (1 << 5),  /**< server_up */
    SYNCED_DOWN_EVENT = (1 << 6),  /**< synced_down */
    SYNCED_UP_EVENT   = (1 << 7),  /**< synced_up */
    DONOR_DOWN_EVENT  = (1 << 8),  /**< donor_down */
    DONOR_UP_EVENT    = (1 << 9),  /**< donor_up */
    NDB_DOWN_EVENT    = (1 << 10), /**< ndb_down */
    NDB_UP_EVENT      = (1 << 11), /**< ndb_up */
    LOST_MASTER_EVENT = (1 << 12), /**< lost_master */
    LOST_SLAVE_EVENT  = (1 << 13), /**< lost_slave */
    LOST_SYNCED_EVENT = (1 << 14), /**< lost_synced */
    LOST_DONOR_EVENT  = (1 << 15), /**< lost_donor */
    LOST_NDB_EVENT    = (1 << 16), /**< lost_ndb */
    NEW_MASTER_EVENT  = (1 << 17), /**< new_master */
    NEW_SLAVE_EVENT   = (1 << 18), /**< new_slave */
    NEW_SYNCED_EVENT  = (1 << 19), /**< new_synced */
    NEW_DONOR_EVENT   = (1 << 20), /**< new_donor */
    NEW_NDB_EVENT     = (1 << 21), /**< new_ndb */
} monitor_event_t;

static const MXS_ENUM_VALUE monitor_event_enum_values[] =
{
    {"master_down", MASTER_DOWN_EVENT},
    {"master_up", MASTER_UP_EVENT},
    {"slave_down", SLAVE_DOWN_EVENT},
    {"slave_up", SLAVE_UP_EVENT},
    {"server_down", SERVER_DOWN_EVENT},
    {"server_up", SERVER_UP_EVENT},
    {"synced_down", SYNCED_DOWN_EVENT},
    {"synced_up", SYNCED_UP_EVENT},
    {"donor_down", DONOR_DOWN_EVENT},
    {"donor_up", DONOR_UP_EVENT},
    {"ndb_down", NDB_DOWN_EVENT},
    {"ndb_up", NDB_UP_EVENT},
    {"lost_master", LOST_MASTER_EVENT},
    {"lost_slave", LOST_SLAVE_EVENT},
    {"lost_synced", LOST_SYNCED_EVENT},
    {"lost_donor", LOST_DONOR_EVENT},
    {"lost_ndb", LOST_NDB_EVENT},
    {"new_master", NEW_MASTER_EVENT},
    {"new_slave", NEW_SLAVE_EVENT},
    {"new_synced", NEW_SYNCED_EVENT},
    {"new_donor", NEW_DONOR_EVENT},
    {"new_ndb", NEW_NDB_EVENT},
    {NULL}
};

/** Default value for the `events` parameter */
static const char MONITOR_EVENT_DEFAULT_VALUE[] = "master_down,master_up,slave_down,"
                                                  "slave_up,server_down,server_up,synced_down,synced_up,donor_down,donor_up,"
                                                  "ndb_down,ndb_up,lost_master,lost_slave,lost_synced,lost_donor,lost_ndb,"
                                                  "new_master,new_slave,new_synced,new_donor,new_ndb";

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
struct monitor
{
    char *name;                   /**< The name of the monitor module */
    char user[MAX_MONITOR_USER_LEN]; /*< Monitor username */
    char password[MAX_MONITOR_PASSWORD_LEN]; /*< Monitor password */
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
    char *module_name;            /**< Name of the monitor module */
    void *handle;                 /**< Handle returned from startMonitor */
    size_t interval;              /**< The monitor interval */
    bool created_online;          /**< Whether this monitor was created at runtime */
    volatile bool server_pending_changes;
    /**< Are there any pending changes to a server?
       * If yes, the next monitor loop starts early.  */
    struct monitor *next;         /**< Next monitor in the linked list */
};

extern MONITOR *monitor_alloc(char *, char *);
extern void monitor_free(MONITOR *);
extern MONITOR *monitor_find(const char *);
extern bool monitorAddServer(MONITOR *mon, SERVER *server);
extern void monitorRemoveServer(MONITOR *mon, SERVER *server);
extern void monitorAddUser(MONITOR *, char *, char *);
extern void monitorAddParameters(MONITOR *monitor, CONFIG_PARAMETER *params);
extern bool monitorRemoveParameter(MONITOR *monitor, const char *key);
extern void monitorStop(MONITOR *);
extern void monitorStart(MONITOR *, void*);
extern void monitorStopAll();
extern void monitorStartAll();
extern void monitorShowAll(DCB *);
extern void monitorShow(DCB *, MONITOR *);
extern void monitorList(DCB *);
extern void monitorSetInterval (MONITOR *, unsigned long);
extern bool monitorSetNetworkTimeout(MONITOR *, int, int);
extern RESULTSET *monitorGetList();
extern bool check_monitor_permissions(MONITOR* monitor, const char* query);

monitor_event_t mon_get_event_type(MONITOR_SERVERS* node);
const char* mon_get_event_name(MONITOR_SERVERS* node);
void monitor_clear_pending_status(MONITOR_SERVERS *ptr, int bit);
void monitor_set_pending_status(MONITOR_SERVERS *ptr, int bit);
bool mon_status_changed(MONITOR_SERVERS* mon_srv);
bool mon_print_fail_status(MONITOR_SERVERS* mon_srv);
connect_result_t mon_connect_to_db(MONITOR* mon, MONITOR_SERVERS *database);
void mon_log_connect_error(MONITOR_SERVERS* database, connect_result_t rval);
void mon_log_state_change(MONITOR_SERVERS *ptr);
void lock_monitor_servers(MONITOR *monitor);
void release_monitor_servers(MONITOR *monitor);
void servers_status_pending_to_current(MONITOR *monitor);
void servers_status_current_to_pending(MONITOR *monitor);

/**
 * @brief Handle state change events
 *
 * This function should be called by all monitors at the end of each monitoring
 * cycle. This will log state changes and execute any scripts that should be executed.
 *
 * @param monitor Monitor object
 * @param script Script to execute or NULL for no script
 * @param events Enabled events
 */
void mon_process_state_changes(MONITOR *monitor, const char *script, uint64_t events);

/**
 * @brief Hangup connections to failed servers
 *
 * Injects hangup events for DCB that are connected to servers that are down.
 *
 * @param monitor Monitor object
 */
void mon_hangup_failed_servers(MONITOR *monitor);

/**
 * @brief Serialize the servers of a monitor to a file
 *
 * This partially converts @c monitor into an INI format file. Only the servers
 * of the monitor are serialized. This allows the monitor to keep monitoring
 * the servers that were added at runtime even after a restart.
 *
 * NOTE: This does not persist the complete monitor configuration and requires
 * that an existing monitor configuration is in the main configuration file.
 * Changes to monitor parameters are not persisted.
 *
 * @param monitor Monitor to serialize
 * @return False if the serialization of the monitor fails, true if it was successful
 */
bool monitor_serialize_servers(const MONITOR *monitor);

/**
 * @brief Serialize a monitor to a file
 *
 * This converts the static configuration of the monitor into an INI format file.
 *
 * @param monitor Monitor to serialize
 * @return True if serialization was successful
 */
bool monitor_serialize(const MONITOR *monitor);

/**
 * Check if a server is being monitored and return the monitor.
 * @param server Server that is queried
 * @return The monitor watching this server, or NULL if not monitored
 */
MONITOR* monitor_server_in_use(const SERVER *server);

MXS_END_DECLS
