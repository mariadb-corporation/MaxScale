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
#include <mutex>
#include <openssl/sha.h>
#include <maxbase/semaphore.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/worker.hh>
#include <maxbase/iterator.hh>
#include <maxscale/config.hh>
#include <maxscale/server.hh>
#include <maxscale/protocol/mysql.hh>

namespace maxscale
{
class Monitor;
}

struct DCB;
struct json_t;
struct EXTERNCMD;
class MonitorManager;

/**
 * @verbatim
 * The "module object" structure for a backend monitor module
 *
 * Monitor modules monitor the backend databases that MaxScale connects to.
 * The information provided by a monitor is used in routing decisions.
 * @endverbatim
 *
 * @see load_module
 */
struct MXS_MONITOR_API
{
    /**
     * @brief Create the monitor.
     *
     * This entry point is called once when MaxScale is started, for creating the monitor.
     * If the function fails, MaxScale will not start. The returned object must inherit from
     * the abstract base monitor class and implement the missing methods.
     *
     * @param name Configuration name of the monitor
     * @param module Module name of the monitor
     * @return Monitor object
     */
    maxscale::Monitor* (* createInstance)(const std::string& name, const std::string& module);
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

/**
 * The monitor API version number. Any change to the monitor module API
 * must change these versions using the rules defined in modinfo.h
 */
#define MXS_MONITOR_VERSION {5, 0, 0}

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
    MONITOR_STATE_STOPPED,
    MONITOR_STATE_RUNNING,
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

enum credentials_approach_t
{
    CREDENTIALS_INCLUDE,
    CREDENTIALS_EXCLUDE,
};

namespace maxscale
{

/**
 * The linked list of servers that are being monitored by the monitor module.
 */
class MonitorServer
{
public:
    class ConnectionSettings
    {
    public:
        std::string username;       /**< Monitor username */
        std::string password;       /**< Monitor password */
        int connect_timeout {1};    /**< Connect timeout in seconds for mysql_real_connect */
        int write_timeout {1};      /**< Timeout in seconds for each attempt to write to the server.
                                     *   There are retries and the total effective timeout value is two
                                     *   times the option value. */
        int read_timeout {1};       /**< Timeout in seconds to read from the server. There are retries
                                     *   and the total effective timeout value is three times the
                                     *   option value. */
        int connect_attempts {1};   /**< How many times a connection is attempted */
    };

    /**
     * Maintenance mode request constants.
     */
    static const int NO_CHANGE         = 0;
    static const int MAINT_OFF         = 1;
    static const int MAINT_ON          = 2;
    static const int BEING_DRAINED_OFF = 3;
    static const int BEING_DRAINED_ON  = 4;

    MonitorServer(SERVER* server, const SERVER::DiskSpaceLimits& monitor_limits);

    ~MonitorServer();

    /**
     * Set pending status bits in the monitor server
     *
     * @param bits      The bits to set for the server
     */
    void set_pending_status(uint64_t bits);

    /**
     * Clear pending status bits in the monitor server
     *
     * @param bits      The bits to clear for the server
     */
    void clear_pending_status(uint64_t bits);

    /**
     * Store the current server status to the previous and pending status
     * fields of the monitored server.
     */
    void stash_current_status();

    bool status_changed();
    bool should_print_fail_status();
    void log_connect_error(mxs_connect_result_t rval);

    /**
     * Report query error to log.
     */
    void mon_report_query_error();

    /**
     * Ping or connect to a database. If connection does not exist or ping fails, a new connection is created.
     * This will always leave a valid database handle in the database->con pointer, allowing the user to call
     * MySQL C API functions to find out the reason of the failure.
     *
     * @param settings Connection settings
     * @return Connection status.
     */
    mxs_connect_result_t ping_or_connect(const ConnectionSettings& settings);

    const char* get_event_name();

    /*
     * Determine a monitor event, defined by the difference between the old
     * status of a server and the new status.
     *
     * @param   node                The monitor server data for a particular server
     * @result  monitor_event_t     A monitor event (enum)
     *
     * @note This function must only be called from mon_process_state_changes
     */
    mxs_monitor_event_t get_event_type() const;

    void log_state_change();

    /**
     * Is this server ok to update disk space status. Only checks if the server knows of valid disk space
     * limits settings and that the check has not failed before. Disk space check interval should be
     * checked by the monitor.
     *
     * @return True, if the disk space should be checked, false otherwise.
     */
    bool can_update_disk_space_status() const;

    /**
     * @brief Update the disk space status of a server.
     *
     * After the call, the bit @c SERVER_DISK_SPACE_EXHAUSTED will be set on
     * @c pMonitored_server->pending_status if the disk space is exhausted
     * or cleared if it is not.
     */
    void update_disk_space_status();

    SERVER*         server = nullptr;      /**< The server being monitored */
    MYSQL*          con = nullptr;         /**< The MySQL connection */
    bool            log_version_err = true;
    int             mon_err_count = 0;

    uint64_t        mon_prev_status = -1;      /**< Status before starting the current monitor loop */
    uint64_t        pending_status = 0;        /**< Status during current monitor loop */

    int             status_request = NO_CHANGE;  /**< Is admin requesting Maintenance=ON/OFF on the
                                                   *  server? */
private:
    const SERVER::DiskSpaceLimits& monitor_limits; /**< Monitor-level disk-space limits */

    bool ok_to_check_disk_space {true}; /**< Set to false if check fails */
};

/**
 * Representation of the running monitor.
 */
class Monitor
{
public:
    Monitor(const std::string& name, const std::string& module);
    virtual ~Monitor();

    static const int STATUS_FLAG_NOCHECK = 0;
    static const int STATUS_FLAG_CHECK   = -1;

    /**
     * Ping or connect to a database. If connection does not exist or ping fails, a new connection
     * is created. This will always leave a valid database handle in @c *ppCon, allowing the user
     * to call MySQL C API functions to find out the reason of the failure.
     *
     * @param sett        Connection settings
     * @param pServer     A server
     * @param ppConn      Address of pointer to a MYSQL instance. The instance should either be
     *                    valid or NULL.
     * @return Connection status.
     */
    static mxs_connect_result_t ping_or_connect_to_db(const MonitorServer::ConnectionSettings& sett,
                                                      SERVER& server, MYSQL** ppConn);

    static bool connection_is_ok(mxs_connect_result_t connect_result);

    /*
     * Convert a monitor event (enum) to string.
     *
     * @param   event    The event
     * @return  Text description
     */
    static const char* get_event_name(mxs_monitor_event_t event);

    virtual monitor_state_t state() const = 0;

    /**
     * Configure the monitor. Called by monitor creation and altering code. Any inheriting classes
     * should override this with their own configuration processing function. The overriding function
     * should first call the configure() of its immediate base class, similar to constructors.
     *
     * @return True, if the monitor could be started, false otherwise.
     */
    virtual bool configure(const MXS_CONFIG_PARAMETER* params);

    /**
     * Stops the monitor.
     */
    void stop();

    /**
     * Write diagnostic information to a DCB.
     *
     * @param dcb      The dcb to write to.
     */
    virtual void diagnostics(DCB* dcb) const = 0;

    /**
     * Return diagnostic information about the monitor
     *
     * @return A JSON object representing the state of the monitor
     * @see jansson.h
     */
    virtual json_t* diagnostics_json() const = 0;

    /**
     * Set disk space threshold setting.
     *
     * @param dst_setting  The disk space threshold as specified in the config file.
     * @return True, if the provided string is valid and the threshold could be set.
     */
    bool set_disk_space_threshold(const std::string& dst_setting);

    /**
     * Set status of monitored server.
     *
     * @param srv   Server, must be monitored by this monitor.
     * @param bit   The server status bit to be sent.
     * @errmsg_out  If the setting of the bit fails, on return the human readable
     *              reason why it could not be set.
     *
     * @return True, if the bit could be set.
     */
    bool set_server_status(SERVER* srv, int bit, std::string* errmsg_out);

    /**
     * Clear status of monitored server.
     *
     * @param srv   Server, must be monitored by this monitor.
     * @param bit   The server status bit to be cleared.
     * @errmsg_out  If the clearing of the bit fails, on return the human readable
     *              reason why it could not be cleared.
     *
     * @return True, if the bit could be cleared.
     */
    bool clear_server_status(SERVER* srv, int bit, std::string* errmsg_out);

    /**
     * Create a list of running servers
     *
     * @param dest Destination where the string is appended, must be null terminated
     * @param len Length of @c dest
     * @param approach Whether credentials should be included or not.
     */
    void append_node_names(char* dest, int len, int status,
                           credentials_approach_t approach = CREDENTIALS_EXCLUDE);

    void show(DCB* dcb);

    const char* const m_name;           /**< Monitor instance name. TODO: change to string */
    const std::string m_module;         /**< Name of the monitor module */
    bool              m_active {true};  /**< True if monitor exists and has not been "destroyed". */

    mutable std::mutex m_lock;

    /** Set when admin requests a maintenance status change. */
    int check_status_flag = STATUS_FLAG_NOCHECK;

    uint64_t m_ticks {0};                         /**< Number of performed monitoring intervals */
    uint8_t  m_journal_hash[SHA_DIGEST_LENGTH];   /**< SHA1 hash of the latest written journal */

    MXS_CONFIG_PARAMETER parameters;             /**< Configuration parameters */
    std::vector<MonitorServer*> m_servers;       /**< Monitored servers */

protected:
    /**
     * Stop the monitor. If the monitor uses a polling thread, the thread should be stopped.
     */
    virtual void do_stop() = 0;

    /**
     * Check if the monitor user can execute a query. The query should be such that it only succeeds if
     * the monitor user has all required permissions. Servers which are down are skipped.
     *
     * @param query Query to test with
     * @return True on success, false if monitor credentials lack permissions
     */
    bool test_permissions(const std::string& query);

    /**
     * Detect and handle state change events. This function should be called by all monitors at the end
     * of each monitoring cycle. The function logs state changes and executes the monitor script on
     * servers whose status changed.
     */
    void detect_handle_state_changes();

    /**
     * Is the journal stale?
     *
     * @return True, if the journal is stale, false otherwise.
     */
    bool journal_is_stale() const;

    /**
     * @brief Called when a server has been added to the monitor.
     *
     * The default implementation will add the server to associated
     * services.
     *
     * @param server  A server.
     */
    virtual void server_added(SERVER* server);

    /**
     * @brief Called when a server has been removed from the monitor.
     *
     * The default implementation will remove the server from associated
     * services.
     *
     * @param server  A server.
     */
    virtual void server_removed(SERVER* server);

    /**
     * Get an array of monitored servers. If a server defined in the config setting is not monitored by
     * this monitor, the returned array will be empty.
     *
     * @param key Setting name
     * @param error_out Set to true if an error occurs
     * @return Output array
     */
    std::vector<MonitorServer*> get_monitored_serverlist(const std::string& key, bool* error_out);

    /**
     * Find the monitored server representing the server.
     *
     * @param search_server Server to search for
     * @return Found monitored server or NULL if not found
     */
    MonitorServer* get_monitored_server(SERVER* search_server);

    /**
     * @brief Load a journal of server states
     *
     * @param master  Set to point to the current master
     */
    void load_server_journal(MonitorServer** master);

    /**
     * @brief Store a journal of server states
     *
     * @param master  The current master server or NULL if no master exists
     */
    void store_server_journal(MonitorServer* master);

    void check_maintenance_requests();

    /**
     * @brief Hangup connections to failed servers
     *
     * Injects hangup events for DCB that are connected to servers that are down.
     */
    void hangup_failed_servers();

    void remove_server_journal();

    MonitorServer* find_parent_node(MonitorServer* target);

    std::string child_nodes(MonitorServer* parent);

    /**
     * Checks if it's time to check disk space. If true is returned, the internal timer is reset
     * so that the next true is only returned once disk_space_check_interval has again passed.
     *
     * @return True if disk space should be checked
     */
    bool check_disk_space_this_tick();

    /**
     * Contains monitor base class settings. Since monitors are stopped before a setting change,
     * the items cannot be modified while a monitor is running. No locking required.
     */
    class Settings
    {
    public:
        int64_t     interval {0};        /**< Monitor interval in milliseconds */

        std::string script;              /**< Script triggered by events */
        int         script_timeout {0};  /**< Timeout in seconds for the monitor scripts */
        uint64_t    events {0};          /**< Bitfield of events which trigger the script */

        time_t      journal_max_age {0}; /**< Maximum age of journal file */

        SERVER::DiskSpaceLimits  disk_space_limits;     /**< Disk space thresholds */

        // How often should a disk space check be made at most. Negative values imply disabling.
        maxbase::Duration disk_space_check_interval {-1};

        MonitorServer::ConnectionSettings conn_settings;
    };

    Settings m_settings;

private:
    friend class ::MonitorManager;

    /**
     * @brief Add a server to a monitor.
     *
     * Add a server to a monitor, provided the server is not currently
     * being monitored by any monitor. Before adding the server to the
     * monitor, the monitor is stopped if it is running and after the
     * addition it is restarted if it was running.
     *
     * @param server   A server.
     *
     * @return True, if the monitor was added, false otherwise.
     */
    void add_server(SERVER* server);

    void remove_server(SERVER* server);

    /**
     * Starts the monitor. If the monitor requires polling of the servers, it should create
     * a separate monitoring thread.
     *
     * @return True, if the monitor could be started, false otherwise.
     */
    virtual bool start() = 0;

    /**
     * Launch a script
     *
     * @param ptr     The server which has changed state
     * @return Return value of the executed script or -1 on error
     */
    int launch_script(MonitorServer* ptr);

    /**
     * Launch a command
     *
     * @param ptr  The server which has changed state
     * @param cmd  The command to execute.
     *
     * @note All default script variables will be replaced.
     *
     * @return Return value of the executed script or -1 on error.
     */
    int launch_command(MonitorServer* ptr, EXTERNCMD* cmd);

    /**
     * @brief The monitor should populate associated services.
     */
    virtual void populate_services();

    FILE* open_data_file(Monitor* monitor, char* path);
    int get_data_file_path(char* path) const;

    mxb::StopWatch m_disk_space_checked;    /**< When was disk space checked the last time */
};

/**
 * An abstract class which helps implement a monitor based on a maxbase::Worker thread.
 */
class MonitorWorker : public Monitor
                    , protected maxbase::Worker
{
public:
    MonitorWorker(const MonitorWorker&) = delete;
    MonitorWorker& operator=(const MonitorWorker&) = delete;

    virtual ~MonitorWorker();

    /**
     * @brief Current state of the monitor.
     *
     * Since the state is written to by the admin thread, the value returned in other threads cannot be fully
     * trusted. The state should only be read in the admin thread or operations launched by the admin thread.
     *
     * @return @c MONITOR_STATE_RUNNING if the monitor is running,
     *         @c MONITOR_STATE_STOPPED if the monitor is stopped.
     */
    monitor_state_t state() const override final;

    /**
     * @brief Find out whether the monitor is running.
     *
     * @return True, if the monitor is running, false otherwise.
     *
     * @see state().
     */
    bool is_running() const
    {
        return state() == MONITOR_STATE_RUNNING;
    }

    /**
     * @brief Starts the monitor.
     *
     * - Calls @c has_sufficient_permissions(), if it has not been done earlier.
     * - Updates the 'script' and 'events' configuration paramameters.
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
     * @return True, if the monitor started, false otherwise.
     */
    bool start() final;

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
    MonitorWorker(const std::string& name, const std::string& module);

    void do_stop() final;

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
    bool configure(const MXS_CONFIG_PARAMETER* pParams) override;

    /**
     * @brief Check whether the monitor has sufficient rights
     *
     * The implementation should check whether the monitor user has sufficient
     * rights to access the servers. The default implementation returns True.
     *
     * @return True, if the monitor user has sufficient rights, false otherwise.
     */
    virtual bool has_sufficient_permissions();

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
     * The default implementation will call @Monitor::detect_state_changes. Overriding functions
     * should do the same before proceeding with their own processing.
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

    MonitorServer* m_master;     /**< Master server */

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

class MonitorWorkerSimple : public MonitorWorker
{
public:
    MonitorWorkerSimple(const MonitorWorkerSimple&) = delete;
    MonitorWorkerSimple& operator=(const MonitorWorkerSimple&) = delete;

protected:
    MonitorWorkerSimple(const std::string& name, const std::string& module)
        : MonitorWorker(name, module)
    {
    }

    /**
     * @brief Update server information
     *
     * The implementation should probe the server in question and update
     * the server status bits.
     */
    virtual void update_server_status(MonitorServer* pMonitored_server) = 0;

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

    static Monitor* createInstance(const std::string& name, const std::string& module)
    {
        MonitorInstance* pInstance = NULL;
        MXS_EXCEPTION_GUARD(pInstance = MonitorInstance::create(name, module));
        return pInstance;
    }

    static MXS_MONITOR_API s_api;
};

template<class MonitorInstance>
MXS_MONITOR_API MonitorApi<MonitorInstance>::s_api =
{
    &MonitorApi<MonitorInstance>::createInstance,
};
}
