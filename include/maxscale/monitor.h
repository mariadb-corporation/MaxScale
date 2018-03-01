#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file include/maxscale/monitor.h - The public monitor interface
 */

#include <maxscale/cdefs.h>

#include <mysql.h>

#include <maxscale/config.h>
#include <maxscale/dcb.h>
#include <maxscale/server.h>

MXS_BEGIN_DECLS

struct mxs_monitor;
typedef struct mxs_monitor MXS_MONITOR;

/**
 * @verbatim
 * The "module object" structure for a backend monitor module
 *
 * Monitor modules monitor the backend databases that MaxScale connects to.
 * The information provided by a monitor is used in routing decisions.
 *
 * The entry points are:
 *      startMonitor    Called by main to start the monitor
 *      stopMonitor     Called by main to shut down and destroy a monitor
 *      diagnostics     Called for diagnostic output
 *
 * startMonitor is called to start the monitoring process, it is called on the
 * MaxScale main thread and is responsible for creating a thread for the monitor
 * itself to run on. This should use the entry points defined in the thread.h
 * header file rather than make direct calls to the operating system threading
 * libraries. The return from startMonitor is a pointer that will be passed to
 * all other monitor API calls.
 *
 * @endverbatim
 *
 * @see load_module
 */
typedef struct mxs_monitor_object
{
    void *(*startMonitor)(MXS_MONITOR *monitor, const MXS_CONFIG_PARAMETER *params);
    void (*stopMonitor)(MXS_MONITOR *monitor);
    void (*diagnostics)(DCB *, const MXS_MONITOR *);
} MXS_MONITOR_OBJECT;

/**
 * The monitor API version number. Any change to the monitor module API
 * must change these versions using the rules defined in modinfo.h
 */
#define MXS_MONITOR_VERSION {3, 0, 0}

/** Monitor's poll frequency */
#define MXS_MON_BASE_INTERVAL_MS 100

#define MXS_MONITOR_RUNNING 1
#define MXS_MONITOR_STOPPING 2
#define MXS_MONITOR_STOPPED 3

#define MXS_MONITOR_DEFAULT_ID 1UL // unsigned long value

#define MAX_MONITOR_USER_LEN     512
#define MAX_MONITOR_PASSWORD_LEN 512

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

/*
 * Results of attempt at database connection for monitoring
 */
typedef enum
{
    MONITOR_CONN_OK,
    MONITOR_CONN_REFUSED,
    MONITOR_CONN_TIMEOUT
} mxs_connect_result_t;

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
} mxs_monitor_event_t;

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
} MXS_MONITOR_SERVERS;

/**
 * Representation of the running monitor.
 */
struct mxs_monitor
{
    char *name;                   /**< The name of the monitor module */
    char user[MAX_MONITOR_USER_LEN]; /*< Monitor username */
    char password[MAX_MONITOR_PASSWORD_LEN]; /*< Monitor password */
    SPINLOCK lock;
    MXS_CONFIG_PARAMETER* parameters; /*< configuration parameters */
    MXS_MONITOR_SERVERS* databases; /*< List of databases the monitor monitors */
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
    MXS_MONITOR_OBJECT *module;   /**< The "monitor object" */
    char *module_name;            /**< Name of the monitor module */
    void *handle;                 /**< Handle returned from startMonitor */
    size_t interval;              /**< The monitor interval */
    bool created_online;          /**< Whether this monitor was created at runtime */
    volatile bool server_pending_changes;
    /**< Are there any pending changes to a server?
       * If yes, the next monitor loop starts early.  */
    struct mxs_monitor *next;     /**< Next monitor in the linked list */
};

static const MXS_ENUM_VALUE mxs_monitor_event_enum_values[] =
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
static const char MXS_MONITOR_EVENT_DEFAULT_VALUE[] = "master_down,master_up,slave_down,"
                                                      "slave_up,server_down,server_up,synced_down,synced_up,donor_down,donor_up,"
                                                      "ndb_down,ndb_up,lost_master,lost_slave,lost_synced,lost_donor,lost_ndb,"
                                                      "new_master,new_slave,new_synced,new_donor,new_ndb";

bool check_monitor_permissions(MXS_MONITOR* monitor, const char* query);

void monitor_clear_pending_status(MXS_MONITOR_SERVERS *ptr, int bit);
void monitor_set_pending_status(MXS_MONITOR_SERVERS *ptr, int bit);
void servers_status_pending_to_current(MXS_MONITOR *monitor);
void servers_status_current_to_pending(MXS_MONITOR *monitor);

bool mon_status_changed(MXS_MONITOR_SERVERS* mon_srv);
bool mon_print_fail_status(MXS_MONITOR_SERVERS* mon_srv);

mxs_connect_result_t mon_connect_to_db(MXS_MONITOR* mon, MXS_MONITOR_SERVERS *database);
void mon_log_connect_error(MXS_MONITOR_SERVERS* database, mxs_connect_result_t rval);

void lock_monitor_servers(MXS_MONITOR *monitor);
void release_monitor_servers(MXS_MONITOR *monitor);

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
void mon_process_state_changes(MXS_MONITOR *monitor, const char *script, uint64_t events);

/**
 * @brief Hangup connections to failed servers
 *
 * Injects hangup events for DCB that are connected to servers that are down.
 *
 * @param monitor Monitor object
 */
void mon_hangup_failed_servers(MXS_MONITOR *monitor);

/**
 * @brief Report query errors
 *
 * @param db Database where the query failed
 */
void mon_report_query_error(MXS_MONITOR_SERVERS* db);

MXS_END_DECLS
