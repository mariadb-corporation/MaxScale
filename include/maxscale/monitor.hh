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
 * @file include/maxscale/monitor.hh - The public monitor interface
 */

#include <maxscale/ccdefs.hh>

#include <atomic>
#include <openssl/sha.h>
#include <maxbase/semaphore.hh>
#include <maxbase/worker.hh>
#include <maxbase/iterator.hh>
#include <maxbase/jansson.h>
#include <maxscale/config.hh>
#include <maxscale/dcb.hh>
#include <maxscale/server.hh>
#include <maxscale/protocol/mysql.hh>

struct MXS_MONITOR;

/**
 * An opaque type representing a monitor instance.
 */
struct MXS_MONITOR_INSTANCE
{
};

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
struct MXS_MONITOR_API
{
    /**
     * @brief Create the monitor.
     *
     * This entry point is called once when MaxScale is started, for
     * creating the monitor.
     *
     * If the function fails, MaxScale will not start. That is, it
     * should fail only for fatal reasons such as not being able to
     * create vital resources.
     *
     * @param monitor  The monitor object.
     *
     * @return Pointer to the monitor specific data. Will be stored
     *         in @c monitor->handle.
     */
    MXS_MONITOR_INSTANCE*(*createInstance)(MXS_MONITOR * monitor);

    /**
     * @brief Destroy the monitor.
     *
     * This entry point is called once when MaxScale is shutting down, iff
     * the earlier call to @c initMonitor returned on object. The monitor should
     * perform all needed cleanup.
     *
     * @param monitor  The monitor object.
     */
    void (* destroyInstance)(MXS_MONITOR_INSTANCE* monitor);

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
     * @return True, if the monitor could be started, false otherwise.
     */
    bool (* startMonitor)(MXS_MONITOR_INSTANCE* monitor,
                          const MXS_CONFIG_PARAMETER* params);

    /**
     * @brief Stop the monitor
     *
     * This entry point is called when the monitor is stopped. If the monitor
     * uses a polling thread, the thread should be stopped.
     *
     * @param monitor The monitor object
     */
    void (* stopMonitor)(MXS_MONITOR_INSTANCE* monitor);

    /**
     * @brief Write diagnostic information to a DCB.
     *
     * @param monitor  The monitor object.
     * @param dcb      The dcb to write to.
     */
    void (* diagnostics)(const MXS_MONITOR_INSTANCE* monitor, DCB* dcb);

    /**
     * @brief Return diagnostic information about the monitor
     *
     * @param monitor  The monitor object.
     *
     * @return A JSON object representing the state of the monitor
     *
     * @see jansson.h
     */
    json_t* (*diagnostics_json)(const MXS_MONITOR_INSTANCE * monitor);
};

/**
 * The monitor API version number. Any change to the monitor module API
 * must change these versions using the rules defined in modinfo.h
 */
#define MXS_MONITOR_VERSION {4, 0, 0}

/**
 * Specifies capabilities specific for monitor.
 *
 * @see enum routing_capability
 *
 * @note The values of the capabilities here *must* be between 0x0001 0000 0000 0000
 *       and 0x0080 0000 0000 0000, that is, bits 48 to 55.
 */
enum monitor_capability_t
{
    MCAP_TYPE_NONE = 0x0    // TODO: remove once monitor capabilities are defined
};

// Monitor state enum
enum monitor_state_t
{
    MONITOR_STATE_RUNNING,
    MONITOR_STATE_STOPPING,
    MONITOR_STATE_STOPPED
};

/* Return type of mon_ping_or_connect_to_db(). */
enum mxs_connect_result_t
{
    MONITOR_CONN_EXISTING_OK,   /* Existing connection was ok and server replied to ping. */
    MONITOR_CONN_NEWCONN_OK,    /* No existing connection or no ping reply. New connection created
                                 * successfully. */
    MONITOR_CONN_REFUSED,       /* No existing connection or no ping reply. Server refused new connection. */
    MONITOR_CONN_TIMEOUT        /* No existing connection or no ping reply. Timeout on new connection. */
};

/** Monitor events */
enum mxs_monitor_event_t
{
    UNDEFINED_EVENT   = 0,
    MASTER_DOWN_EVENT = (1 << 0),   /**< master_down */
    MASTER_UP_EVENT   = (1 << 1),   /**< master_up */
    SLAVE_DOWN_EVENT  = (1 << 2),   /**< slave_down */
    SLAVE_UP_EVENT    = (1 << 3),   /**< slave_up */
    SERVER_DOWN_EVENT = (1 << 4),   /**< server_down */
    SERVER_UP_EVENT   = (1 << 5),   /**< server_up */
    SYNCED_DOWN_EVENT = (1 << 6),   /**< synced_down */
    SYNCED_UP_EVENT   = (1 << 7),   /**< synced_up */
    DONOR_DOWN_EVENT  = (1 << 8),   /**< donor_down */
    DONOR_UP_EVENT    = (1 << 9),   /**< donor_up */
    NDB_DOWN_EVENT    = (1 << 10),  /**< ndb_down */
    NDB_UP_EVENT      = (1 << 11),  /**< ndb_up */
    LOST_MASTER_EVENT = (1 << 12),  /**< lost_master */
    LOST_SLAVE_EVENT  = (1 << 13),  /**< lost_slave */
    LOST_SYNCED_EVENT = (1 << 14),  /**< lost_synced */
    LOST_DONOR_EVENT  = (1 << 15),  /**< lost_donor */
    LOST_NDB_EVENT    = (1 << 16),  /**< lost_ndb */
    NEW_MASTER_EVENT  = (1 << 17),  /**< new_master */
    NEW_SLAVE_EVENT   = (1 << 18),  /**< new_slave */
    NEW_SYNCED_EVENT  = (1 << 19),  /**< new_synced */
    NEW_DONOR_EVENT   = (1 << 20),  /**< new_donor */
    NEW_NDB_EVENT     = (1 << 21),  /**< new_ndb */
};

/**
 * The linked list of servers that are being monitored by the monitor module.
 */
struct MXS_MONITORED_SERVER
{
    SERVER*                  server;/**< The server being monitored */
    MYSQL*                   con;   /**< The MySQL connection */
    bool                     log_version_err;
    int                      mon_err_count;
    uint64_t                 mon_prev_status;   /**< Status before starting the current monitor loop */
    uint64_t                 pending_status;    /**< Status during current monitor loop */
    int64_t                  disk_space_checked;/**< When was the disk space checked the last time */
    struct MXS_MONITORED_SERVER* next;              /**< The next server in the list */
};

namespace std
{

inline mxb::intrusive_slist_iterator<MXS_MONITORED_SERVER> begin(MXS_MONITORED_SERVER& monitored_server)
{
    return mxb::intrusive_slist_iterator<MXS_MONITORED_SERVER>(monitored_server);
}

inline mxb::intrusive_slist_iterator<MXS_MONITORED_SERVER> end(MXS_MONITORED_SERVER& monitored_server)
{
    return mxb::intrusive_slist_iterator<MXS_MONITORED_SERVER>();
}

}

#define MAX_MONITOR_USER_LEN     512
#define MAX_MONITOR_PASSWORD_LEN 512

/**
 * Representation of the running monitor.
 */
struct MXS_MONITOR
{
    char*                 name;                                 /**< The name of the monitor module */
    char                  user[MAX_MONITOR_USER_LEN];           /*< Monitor username */
    char                  password[MAX_MONITOR_PASSWORD_LEN];   /*< Monitor password */
    pthread_mutex_t       lock;
    MXS_CONFIG_PARAMETER* parameters;                       /*< configuration parameters */
    MXS_MONITORED_SERVER* monitored_servers;                /*< List of servers the monitor monitors */
    monitor_state_t       state;                            /**< The state of the monitor. This should ONLY be
                                                             * written to by the admin
                                                             *   thread. */
    int connect_timeout;                                    /**< Connect timeout in seconds for
                                                             * mysql_real_connect */
    int connect_attempts;                                   /**< How many times a connection is attempted */
    int read_timeout;                                       /**< Timeout in seconds to read from the server.
                                                             *   There are retries and the total effective
                                                             * timeout
                                                             *   value is three times the option value.
                                                             */
    int write_timeout;                                      /**< Timeout in seconds for each attempt to write
                                                             * to the server.
                                                             * There are retries and the total effective
                                                             * timeout value is
                                                             * two times the option value.
                                                             */
    MXS_MONITOR_API*      api;                              /**< The monitor api */
    char*                 module_name;                      /**< Name of the monitor module */
    MXS_MONITOR_INSTANCE* instance;                         /**< Instance returned from startMonitor */
    size_t                interval;                         /**< The monitor interval */
    int                   check_maintenance_flag;           /**< Set when admin requests a maintenance status
                                                             * change. */
    bool                   active;                          /**< True if monitor is active */
    time_t                 journal_max_age;                 /**< Maximum age of journal file */
    uint32_t               script_timeout;                  /**< Timeout in seconds for the monitor scripts */
    const char*            script;                          /**< Launchable script. */
    uint64_t               events;                          /**< Enabled monitor events. */
    uint8_t                journal_hash[SHA_DIGEST_LENGTH]; /**< SHA1 hash of the latest written journal */
    MxsDiskSpaceThreshold* disk_space_threshold;            /**< Disk space thresholds */
    int64_t                disk_space_check_interval;       /**< How often should a disk space check be made
                                                             * at most. */
    uint64_t            ticks;                              /**< Number of performed monitoring intervals */
    struct MXS_MONITOR* next;                               /**< Next monitor in the linked list */
};

/**
 * Monitor configuration parameters names
 */
extern const char CN_BACKEND_CONNECT_ATTEMPTS[];
extern const char CN_BACKEND_CONNECT_TIMEOUT[];
extern const char CN_BACKEND_READ_TIMEOUT[];
extern const char CN_BACKEND_WRITE_TIMEOUT[];
extern const char CN_DISK_SPACE_CHECK_INTERVAL[];
extern const char CN_EVENTS[];
extern const char CN_JOURNAL_MAX_AGE[];
extern const char CN_MONITOR_INTERVAL[];
extern const char CN_SCRIPT[];
extern const char CN_SCRIPT_TIMEOUT[];

bool check_monitor_permissions(MXS_MONITOR* monitor, const char* query);

void monitor_clear_pending_status(MXS_MONITORED_SERVER* ptr, uint64_t bit);
void monitor_set_pending_status(MXS_MONITORED_SERVER* ptr, uint64_t bit);
void monitor_check_maintenance_requests(MXS_MONITOR* monitor);

bool mon_status_changed(MXS_MONITORED_SERVER* mon_srv);
bool mon_print_fail_status(MXS_MONITORED_SERVER* mon_srv);

mxs_connect_result_t mon_ping_or_connect_to_db(MXS_MONITOR* mon, MXS_MONITORED_SERVER* database);
bool                 mon_connection_is_ok(mxs_connect_result_t connect_result);
void                 mon_log_connect_error(MXS_MONITORED_SERVER* database, mxs_connect_result_t rval);
const char*          mon_get_event_name(mxs_monitor_event_t event);

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
void mon_process_state_changes(MXS_MONITOR* monitor, const char* script, uint64_t events);

/**
 * @brief Hangup connections to failed servers
 *
 * Injects hangup events for DCB that are connected to servers that are down.
 *
 * @param monitor Monitor object
 */
void mon_hangup_failed_servers(MXS_MONITOR* monitor);

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
void store_server_journal(MXS_MONITOR* monitor, MXS_MONITORED_SERVER* master);

/**
 * @brief Load a journal of server states
 *
 * @param monitor Monitor where journal is loaded
 * @param master  Set to point to the current master
 */
void load_server_journal(MXS_MONITOR* monitor, MXS_MONITORED_SERVER** master);

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
int mon_config_get_servers(const MXS_CONFIG_PARAMETER* params,
                           const char* key,
                           const MXS_MONITOR* mon,
                           MXS_MONITORED_SERVER*** monitored_array_out);

/**
 * @brief Set the disk space threshold of a monitor
 *
 * @param server                The monitor.
 * @param disk_space_threshold  The disk space threshold as specified in the config file.
 *
 * @return True, if the provided string is valid and the threshold could be set.
 */
bool monitor_set_disk_space_threshold(MXS_MONITOR* monitor, const char* disk_space_threshold);

// Function for waiting one monitor interval
void monitor_debug_wait();

namespace maxscale
{

class MonitorInstance : public MXS_MONITOR_INSTANCE
                      , protected maxbase::Worker
{
public:
    MonitorInstance(const MonitorInstance&) = delete;
    MonitorInstance& operator=(const MonitorInstance&) = delete;

    virtual ~MonitorInstance();

    /**
     * @brief Current state of the monitor.
     *
     * Since the state is written to by the admin thread, the value returned in other threads cannot be fully
     * trusted. The state should only be read in the admin thread or operations launched by the admin thread.
     *
     * @return @c MONITOR_STATE_RUNNING if the monitor is running,
     *         @c MONITOR_STATE_STOPPING if the monitor is stopping, and
     *         @c MONITOR_STATE_STOPPED if the monitor is stopped.
     */
    monitor_state_t monitor_state() const;

    /**
     * @brief Find out whether the monitor is running.
     *
     * @return True, if the monitor is running, false otherwise.
     *
     * @see state().
     */
    bool is_running() const
    {
        return monitor_state() == MONITOR_STATE_RUNNING;
    }

    /**
     * @brief Starts the monitor.
     *
     * - Calls @c has_sufficient_permissions(), if it has not been done earlier.
     * - Updates the 'script' and 'events' configuration paramameters.
     * - Calls @c configure().
     * - Starts the monitor thread.
     *
     * - Once the monitor thread starts, it will
     *   - Load the server journal and update @c m_master.
     *   - Call @c pre_loop().
     *   - Enter a loop where it, until told to shut down, will
     *     - Check whether there are maintenance requests.
     *     - Call @c tick().
     *     - Call @c process_state_changes()
     *     - Hang up failed servers.
     *     - Store the server journal (@c m_master assumed to reflect the current situation).
     *     - Sleep until time for next @c tick().
     *   - Call @c post_loop().
     *
     * @param param  The parameters of the monitor.
     *
     * @return True, if the monitor started, false otherwise.
     */
    bool start(const MXS_CONFIG_PARAMETER* params);

    /**
     * @brief Stops the monitor.
     *
     * When the function returns, the monitor has stopped.
     */
    void stop();

    /**
     * @brief Write diagnostics
     *
     * The implementation should write diagnostic information to the
     * provided dcb. The default implementation writes nothing.
     *
     * @param dcb  The dcb to write to.
     */
    virtual void diagnostics(DCB* dcb) const;

    /**
     * @brief Obtain diagnostics
     *
     * The implementation should create a JSON object and fill it with diagnostics
     * information. The default implementation returns an object that is populated
     * with the keys 'script' and 'events' if they have been set, otherwise the
     * object is empty.
     *
     * @return An object, if there is information to return, NULL otherwise.
     */
    virtual json_t* diagnostics_json() const;

    /**
     * Get current time from the monotonic clock.
     *
     * @return Current time
     */
    static int64_t get_time_ms();

protected:
    MonitorInstance(MXS_MONITOR* pMonitor);

    /**
     * @brief Should the monitor shut down?
     *
     * @return True, if the monitor should shut down, false otherwise.
     */
    bool should_shutdown() const
    {
        return atomic_load_int32(&m_shutdown) != 0;
    }

    /**
     * @brief Should the disk space status be updated.
     *
     * @param pMonitored_server  The monitored server in question.
     *
     * @return True, if the disk space should be checked, false otherwise.
     */
    bool should_update_disk_space_status(const MXS_MONITORED_SERVER* pMonitored_server) const;

    /**
     * @brief Update the disk space status of a server.
     *
     * After the call, the bit @c SERVER_DISK_SPACE_EXHAUSTED will be set on
     * @c pMonitored_server->pending_status if the disk space is exhausted
     * or cleared if it is not.
     */
    void update_disk_space_status(MXS_MONITORED_SERVER* pMonitored_server);

    /**
     * @brief Configure the monitor.
     *
     * When the monitor is started, this function will be called in order
     * to allow the concrete implementation to configure itself from
     * configuration parameters. The default implementation returns true.
     *
     * @return True, if the monitor could be configured, false otherwise.
     *
     * @note If false is returned, then the monitor will not be started.
     */
    virtual bool configure(const MXS_CONFIG_PARAMETER* pParams);

    /**
     * @brief Check whether the monitor has sufficient rights
     *
     * The implementation should check whether the monitor user has sufficient
     * rights to access the servers. The default implementation returns True.
     *
     * @return True, if the monitor user has sufficient rights, false otherwise.
     */
    virtual bool has_sufficient_permissions() const;

    /**
     * @brief Flush pending server status to each server.
     *
     * This function is expected to flush the pending status to each server.
     * The default implementation simply copies monitored_server->pending_status
     * to server->status.
     */
    virtual void flush_server_status();

    /**
     * @brief Monitor the servers
     *
     * This function is called once per monitor round, and the concrete
     * implementation should probe all servers and set server status bits.
     */
    virtual void tick() = 0;

    /**
     * @brief Called before the monitor loop is started
     *
     * The default implementation does nothing.
     */
    virtual void pre_loop();

    /**
     * @brief Called after the monitor loop has ended.
     *
     * The default implementation does nothing.
     */
    virtual void post_loop();

    /**
     * @brief Called after tick returns
     *
     * The default implementation will call @mon_process_state_changes.
     */
    virtual void process_state_changes();

    /**
     * Should a monitor tick be ran immediately? The base class version always returns false. A monitor can
     * override this to add specific conditions. This function is called every MXS_MON_BASE_INTERVAL_MS
     * (100 ms) by the monitor worker thread, which then runs a monitor tick if true is returned.
     *
     * @return True if tick should be ran
     */
    virtual bool immediate_tick_required() const;

    MXS_MONITOR*          m_monitor;    /**< The generic monitor structure. */
    MXS_MONITORED_SERVER* m_master;     /**< Master server */

private:
    std::atomic<bool> m_thread_running; /**< Thread state. Only visible inside MonitorInstance. */
    int32_t           m_shutdown;       /**< Non-zero if the monitor should shut down. */
    bool              m_checked;        /**< Whether server access has been checked. */
    mxb::Semaphore    m_semaphore;      /**< Semaphore for synchronizing with monitor thread. */
    int64_t           m_loop_called;    /**< When was the loop called the last time. */

    bool pre_run() final;
    void post_run() final;

    bool call_run_one_tick(Worker::Call::action_t action);
    void run_one_tick();
};

class MonitorInstanceSimple : public MonitorInstance
{
public:
    MonitorInstanceSimple(const MonitorInstanceSimple&) = delete;
    MonitorInstanceSimple& operator=(const MonitorInstanceSimple&) = delete;

protected:
    MonitorInstanceSimple(MXS_MONITOR* pMonitor)
        : MonitorInstance(pMonitor)
    {
    }

    /**
     * @brief Update server information
     *
     * The implementation should probe the server in question and update
     * the server status bits.
     */
    virtual void update_server_status(MXS_MONITORED_SERVER* pMonitored_server) = 0;

    /**
     * @brief Called right at the beginning of @c tick().
     *
     * The default implementation does nothing.
     */
    virtual void pre_tick();

    /**
     * @brief Called right before the end of @c tick().
     *
     * The default implementation does nothing.
     */
    virtual void post_tick();

private:
    /**
     * @brief Monitor the servers
     *
     * This function is called once per monitor round and will for each server:
     *
     *   - Do nothing, if the server is in maintenance.
     *   - Store the previous status of the server.
     *   - Set the pending status of the monitored server object
     *     to the status of the corresponding server object.
     *   - Ensure that there is a connection to the server.
     *     If there is, @c update_server_status() is called.
     *     If there is not, the pending status will be updated accordingly and
     *     @c update_server_status() will *not* be called.
     *   - After the call, update the error count of the server if it is down.
     */
    void tick();    // final
};

/**
 * The purpose of the template MonitorApi is to provide an implementation
 * of the monitor C-API. The template is instantiated with a class that
 * provides the actual behaviour of a monitor.
 */
template<class MonitorInstance>
class MonitorApi
{
public:
    MonitorApi() = delete;
    MonitorApi(const MonitorApi&) = delete;
    MonitorApi& operator=(const MonitorApi&) = delete;

    static MXS_MONITOR_INSTANCE* createInstance(MXS_MONITOR* pMonitor)
    {
        MonitorInstance* pInstance = NULL;
        MXS_EXCEPTION_GUARD(pInstance = MonitorInstance::create(pMonitor));
        return pInstance;
    }

    static void destroyInstance(MXS_MONITOR_INSTANCE* pInstance)
    {
        MXS_EXCEPTION_GUARD(delete static_cast<MonitorInstance*>(pInstance));
    }

    static bool startMonitor(MXS_MONITOR_INSTANCE* pInstance,
                             const MXS_CONFIG_PARAMETER* pParams)
    {
        bool started = false;
        MXS_EXCEPTION_GUARD(started = static_cast<MonitorInstance*>(pInstance)->start(pParams));
        return started;
    }

    static void stopMonitor(MXS_MONITOR_INSTANCE* pInstance)
    {
        MXS_EXCEPTION_GUARD(static_cast<MonitorInstance*>(pInstance)->stop());
    }

    static void diagnostics(const MXS_MONITOR_INSTANCE* pInstance, DCB* pDcb)
    {
        MXS_EXCEPTION_GUARD(static_cast<const MonitorInstance*>(pInstance)->diagnostics(pDcb));
    }

    static json_t* diagnostics_json(const MXS_MONITOR_INSTANCE* pInstance)
    {
        json_t* pJson = NULL;
        MXS_EXCEPTION_GUARD(pJson = static_cast<const MonitorInstance*>(pInstance)->diagnostics_json());
        return pJson;
    }

    static MXS_MONITOR_API s_api;
};

template<class MonitorInstance>
MXS_MONITOR_API MonitorApi<MonitorInstance>::s_api =
{
    &MonitorApi<MonitorInstance>::createInstance,
    &MonitorApi<MonitorInstance>::destroyInstance,
    &MonitorApi<MonitorInstance>::startMonitor,
    &MonitorApi<MonitorInstance>::stopMonitor,
    &MonitorApi<MonitorInstance>::diagnostics,
    &MonitorApi<MonitorInstance>::diagnostics_json,
};
}
