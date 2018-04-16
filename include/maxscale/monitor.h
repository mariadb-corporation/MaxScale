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
 * @file include/maxscale/monitor.h - The public monitor interface
 */

#include <maxscale/cdefs.h>

#include <openssl/sha.h>

#include <maxscale/config.h>
#include <maxscale/dcb.h>
#include <maxscale/server.h>
#include <maxscale/jansson.h>
#include <maxscale/protocol/mysql.h>

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
    /**
     * @brief Start the monitor
     *
     * This entry point is called when the monitor is started. If the monitor
     * requires polling of the servers, it should create a separate monitoring
     * thread.
     *
     * @param monitor The monitor object
     * @param params  Parameters for this monitor
     *
     * @return Pointer to the monitor specific data, stored in @c monitor->handle
     */
    void *(*startMonitor)(MXS_MONITOR *monitor, const MXS_CONFIG_PARAMETER *params);

    /**
     * @brief Stop the monitor
     *
     * This entry point is called when the monitor is stopped. If the monitor
     * uses a polling thread, the thread should be stopped.
     *
     * @param monitor The monitor object
     */
    void (*stopMonitor)(MXS_MONITOR *monitor);
    void (*diagnostics)(DCB *, const MXS_MONITOR *);

    /**
     * @brief Return diagnostic information about the monitor
     *
     * @return A JSON object representing the state of the monitor
     *
     * @see jansson.h
     */
    json_t* (*diagnostics_json)(const MXS_MONITOR *monitor);
} MXS_MONITOR_OBJECT;

/**
 * The monitor API version number. Any change to the monitor module API
 * must change these versions using the rules defined in modinfo.h
 */
#define MXS_MONITOR_VERSION {3, 1, 0}

/**
 * Specifies capabilities specific for monitor.
 *
 * @see enum routing_capability
 *
 * @note The values of the capabilities here *must* be between 0x0001 0000 0000 0000
 *       and 0x0080 0000 0000 0000, that is, bits 48 to 55.
 */
typedef enum monitor_capability
{
    MCAP_TYPE_NONE = 0x0 // TODO: remove once monitor capabilities are defined
} monitor_capability_t;

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
typedef struct monitored_server
{
    SERVER *server;                 /**< The server being monitored */
    MYSQL *con;                     /**< The MySQL connection */
    bool log_version_err;
    int mon_err_count;
    unsigned int mon_prev_status;
    unsigned int pending_status;    /**< Pending Status flag bitmap */
    bool new_event;                 /**< Whether an action was taken on the last event */
    struct monitored_server *next;  /**< The next server in the list */
} MXS_MONITORED_SERVER;

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
    MXS_MONITORED_SERVER* monitored_servers; /*< List of servers the monitor monitors */
    monitor_state_t state;        /**< The state of the monitor */
    int connect_timeout;          /**< Connect timeout in seconds for mysql_real_connect */
    int connect_attempts;      /**< How many times a connection is attempted */
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
    volatile bool server_pending_changes;
    /**< Are there any pending changes to a server?
       * If yes, the next monitor loop starts early.  */
    bool active; /**< True if monitor is active */
    time_t journal_max_age; /**< Maximum age of journal file */
    uint32_t script_timeout; /**< Timeout in seconds for the monitor scripts */
    uint8_t journal_hash[SHA_DIGEST_LENGTH]; /**< SHA1 hash of the latest written journal */
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

/**
 * Monitor configuration parameters names
 */
extern const char CN_BACKEND_CONNECT_ATTEMPTS[];
extern const char CN_BACKEND_READ_TIMEOUT[];
extern const char CN_BACKEND_WRITE_TIMEOUT[];
extern const char CN_BACKEND_CONNECT_TIMEOUT[];
extern const char CN_MONITOR_INTERVAL[];
extern const char CN_JOURNAL_MAX_AGE[];
extern const char CN_SCRIPT_TIMEOUT[];
extern const char CN_SCRIPT[];
extern const char CN_EVENTS[];

bool check_monitor_permissions(MXS_MONITOR* monitor, const char* query);

void monitor_clear_pending_status(MXS_MONITORED_SERVER *ptr, int bit);
void monitor_set_pending_status(MXS_MONITORED_SERVER *ptr, int bit);
void servers_status_pending_to_current(MXS_MONITOR *monitor);
void servers_status_current_to_pending(MXS_MONITOR *monitor);

bool mon_status_changed(MXS_MONITORED_SERVER* mon_srv);
bool mon_print_fail_status(MXS_MONITORED_SERVER* mon_srv);

mxs_connect_result_t mon_ping_or_connect_to_db(MXS_MONITOR* mon, MXS_MONITORED_SERVER *database);
void mon_log_connect_error(MXS_MONITORED_SERVER* database, mxs_connect_result_t rval);
const char* mon_get_event_name(mxs_monitor_event_t event);

void lock_monitor_servers(MXS_MONITOR *monitor);
void release_monitor_servers(MXS_MONITOR *monitor);

/**
 * Alter monitor parameters
 *
 * The monitor parameters should not be altered while the monitor is
 * running. To alter a parameter from outside a monitor module, stop the monitor,
 * do the alteration and then restart the monitor. The monitor "owns" the parameters
 * as long as it is running so if the monitor needs to change its own parameters,
 * it can do it without stopping itself.
 *
 * @param monitor Monitor whose parameter is altered
 * @param key     Parameter name to alter
 * @param value   New value for the parameter
 */
void mon_alter_parameter(MXS_MONITOR* monitor, const char* key, const char* value);

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
void mon_report_query_error(MXS_MONITORED_SERVER* db);

/**
 * @brief Convert monitor to JSON
 *
 * @param monitor Monitor to convert
 * @param host    Hostname of this server
 *
 * @return JSON representation of the monitor
 */
json_t* monitor_to_json(const MXS_MONITOR* monitor, const char* host);

/**
 * @brief Convert all monitors to JSON
 *
 * @param host    Hostname of this server
 *
 * @return JSON array containing all monitors
 */
json_t* monitor_list_to_json(const char* host);

/**
 * @brief Get links to monitors that relate to a server
 *
 * @param server Server to inspect
 * @param host   Hostname of this server
 *
 * @return Array of monitor links or NULL if no relations exist
 */
json_t* monitor_relations_to_server(const SERVER* server, const char* host);

/**
 * @brief Store a journal of server states
 *
 * @param monitor Monitor to journal
 * @param master  The current master server or NULL if no master exists
 */
void store_server_journal(MXS_MONITOR *monitor, MXS_MONITORED_SERVER *master);

/**
 * @brief Load a journal of server states
 *
 * @param monitor Monitor where journal is loaded
 * @param master  Set to point to the current master
 */
void load_server_journal(MXS_MONITOR *monitor, MXS_MONITORED_SERVER **master);

/**
 * Find the monitored server representing the server.
 *
 * @param mon Cluster monitor
 * @param search_server Server to search for
 * @return Found monitored server or NULL if not found
 */
MXS_MONITORED_SERVER* mon_get_monitored_server(const MXS_MONITOR* mon, SERVER* search_server);

/**
 * Get an array of monitored servers. If a server defined in the config setting is not monitored by
 * the given monitor, that server is ignored and not inserted into the output array.
 *
 * @param params Config parameters
 * @param key Setting name
 * @param mon Monitor which should monitor the servers
 * @param monitored_servers_out Where to save output array. The caller should free the array, but not the
 * elements. The output must contain NULL before calling this function.
 * @return Output array size.
 */
int mon_config_get_servers(const MXS_CONFIG_PARAMETER* params, const char* key, const MXS_MONITOR* mon,
                           MXS_MONITORED_SERVER*** monitored_array_out);

MXS_END_DECLS
