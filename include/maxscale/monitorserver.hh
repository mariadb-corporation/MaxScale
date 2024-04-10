/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/config_common.hh>
#include <maxscale/target.hh>

typedef struct st_mysql MYSQL;
namespace maxbase
{
class QueryResult;
class Json;
}

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
    LOST_MASTER_EVENT = (1 << 10),  /**< lost_master */
    LOST_SLAVE_EVENT  = (1 << 11),  /**< lost_slave */
    LOST_SYNCED_EVENT = (1 << 12),  /**< lost_synced */
    LOST_DONOR_EVENT  = (1 << 13),  /**< lost_donor */
    NEW_MASTER_EVENT  = (1 << 14),  /**< new_master */
    NEW_SLAVE_EVENT   = (1 << 15),  /**< new_slave */
    NEW_SYNCED_EVENT  = (1 << 16),  /**< new_synced */
    NEW_DONOR_EVENT   = (1 << 17),  /**< new_donor */
    RELAY_UP_EVENT    = (1 << 18),  /**< relay_up */
    RELAY_DOWN_EVENT  = (1 << 19),  /**< relay_down */
    LOST_RELAY_EVENT  = (1 << 20),  /**< lost_relay */
    NEW_RELAY_EVENT   = (1 << 21),  /**< new_relay */
    BLR_UP_EVENT      = (1 << 22),  /**< blr_up */
    BLR_DOWN_EVENT    = (1 << 23),  /**< blr_down */
    LOST_BLR_EVENT    = (1 << 24),  /**< lost_blr */
    NEW_BLR_EVENT     = (1 << 25),  /**< new_blr */
    ALL_EVENTS        =
        (MASTER_DOWN_EVENT | MASTER_UP_EVENT | SLAVE_DOWN_EVENT | SLAVE_UP_EVENT | SERVER_DOWN_EVENT
            | SERVER_UP_EVENT | SYNCED_DOWN_EVENT | SYNCED_UP_EVENT | DONOR_DOWN_EVENT | DONOR_UP_EVENT
            | LOST_MASTER_EVENT | LOST_SLAVE_EVENT | LOST_SYNCED_EVENT | LOST_DONOR_EVENT | NEW_MASTER_EVENT
            | NEW_SLAVE_EVENT | NEW_SYNCED_EVENT | NEW_DONOR_EVENT | RELAY_UP_EVENT | RELAY_DOWN_EVENT
            | LOST_RELAY_EVENT | NEW_RELAY_EVENT | BLR_UP_EVENT | BLR_DOWN_EVENT | LOST_BLR_EVENT
            | NEW_BLR_EVENT),
};

namespace maxscale
{
/**
 * Base class for a monitored server. A monitor may inherit and implement its own server-class.
 */
class MonitorServer
{
public:
    class ConnectionSettings
    {
    public:
        using seconds = std::chrono::seconds;

        std::string username;           /**< Monitor username */
        std::string password;           /**< Monitor password */
        seconds     connect_timeout;    /**< Connector/C connect timeout */
        seconds     write_timeout;      /**< Connector/C write timeout */
        seconds     read_timeout;       /**< Connector/C read timeout */
        int64_t     connect_attempts;   /**< How many times a connection is attempted */
    };

    /**
     * Container shared between the monitor and all its servers. May be read concurrently, but only
     * written when monitor is stopped.
     */
    class SharedSettings
    {
    public:
        ConnectionSettings conn_settings;       /**< Monitor-level connection settings */
        DiskSpaceLimits    monitor_disk_limits; /**< Monitor-level disk space limits */
    };

    /* Return type of mon_ping_or_connect_to_db(). */
    enum class ConnectResult
    {
        OLDCONN_OK,     /* Existing connection was ok and server replied to ping. */
        NEWCONN_OK,     /* No existing connection or no ping reply. New connection created
                         * successfully. */
        REFUSED,        /* No existing connection or no ping reply. Server refused new connection. */
        TIMEOUT,        /* No existing connection or no ping reply. Timeout on new connection. */
        ACCESS_DENIED   /* Server refused new connection due to authentication failure */
    };

    /** Status change requests */
    enum StatusRequest
    {
        NO_CHANGE,
        MAINT_OFF,
        MAINT_ON,
        DRAINING_OFF,
        DRAINING_ON,
        DNS_DONE,
    };

    // When a monitor detects that a server is down, these bits should be cleared.
    static constexpr uint64_t SERVER_DOWN_CLEAR_BITS {SERVER_RUNNING | SERVER_AUTH_ERROR | SERVER_MASTER
                                                      | SERVER_SLAVE | SERVER_RELAY | SERVER_JOINED
                                                      | SERVER_BLR};

    MonitorServer(SERVER* server, const SharedSettings& shared);
    virtual ~MonitorServer() = default;

    /**
     * Is the return value one of the 'OK' values.
     *
     * @param connect_result Return value of mon_ping_or_connect_to_db
     * @return True of connection is ok
     */
    static bool connection_is_ok(MonitorServer::ConnectResult connect_result);

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

    /**
     * Check if server has all the given bits on in 'm_pending_status'.
     */
    bool has_status(uint64_t bits) const;

    /**
     * Check if server has all the given bits on in 'm_prev_status'.
     */
    bool had_status(uint64_t bits) const;

    static bool status_changed(uint64_t before, uint64_t after);

    bool status_changed();
    bool flush_status();
    bool auth_status_changed();
    void log_connect_error(ConnectResult rval);

    /**
     * Ping or connect to a database. If connection does not exist or ping fails, a new connection is created.
     *
     * @return Connection status
     */
    virtual ConnectResult ping_or_connect() = 0;

    /**
     * Close db connection if currently open.
     */
    virtual void close_conn() = 0;

    /**
     * Fetch 'session_track_system_variables' and other variables from the server, if they have not
     * been fetched recently.
     *
     * @return  True, if the variables were fetched, false otherwise.
     */
    bool maybe_fetch_variables();

    /**
     * Update the Uptime status variable of the server
     */
    virtual void fetch_uptime() = 0;

    virtual void check_permissions() = 0;

    const char* get_event_name();

    /**
     * Determine a monitor event, defined by the difference between the old
     * status of a server and the new status.
     *
     * @return The event for this state change
     */
    static mxs_monitor_event_t event_type(uint64_t before, uint64_t after);

    /**
     * Convert a monitor event (enum) to string.
     *
     * @param   event    The event
     * @return  Text description
     */
    static const char* get_event_name(mxs_monitor_event_t event);

    /**
     * Calls event_type with previous and current server state
     *
     * @note This function must only be called from mon_process_state_changes
     */
    mxs_monitor_event_t get_event_type() const;

    void log_state_change(const std::string& reason);

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
     * @c pMonitored_server->m_pending_status if the disk space is exhausted
     * or cleared if it is not.
     */
    virtual void update_disk_space_status() = 0;

    void add_status_request(StatusRequest request);
    void apply_status_requests();

    bool is_database() const;

    mxb::Json journal_data() const;
    void      read_journal_data(const mxb::Json& data);

    using EventList = std::vector<std::string>;

    /**
     * If a monitor module implements custom events, it should override this function so that it returns
     * a list of new events for the current tick. The list should be cleared at the start of a tick.
     *
     * The default implementation returns an empty list.
     *
     * @return New custom events
     */
    virtual const EventList& new_custom_events() const;

    const ConnectionSettings& conn_settings() const;

    static bool is_access_denied_error(int64_t errornum);

    /**
     * Add base class state details to diagnostics output.
     *
     * @param diagnostic_output Output object
     */
    void add_state_details(json_t* diagnostic_output) const;

    SERVER* server = nullptr;       /**< The server being monitored */
    int     mon_err_count = 0;

    int64_t node_id = -1;           /**< Node id, server_id for M/S or local_index for Galera */
    int64_t master_id = -1;         /**< Master server id of this node */

    mxs_monitor_event_t last_event {SERVER_DOWN_EVENT}; /**< The last event that occurred on this server */
    time_t              triggered_at {time(nullptr)};   /**< Time when the last event was triggered */

protected:
    uint64_t m_prev_status = -1;    /**< Status at start of current monitor loop */
    uint64_t m_pending_status = 0;  /**< Status during current monitor loop */

    const SharedSettings& m_shared;     /**< Settings shared between all servers of the monitor */

    std::string m_latest_error;                 /**< Latest connection error */
    bool        m_ok_to_check_disk_space {true};/**< Set to false if check fails */

private:
    std::atomic_int m_status_request {NO_CHANGE};   /**< Status change request from admin */

    bool         should_fetch_variables();
    virtual bool fetch_variables() = 0;
};

/**
 * Base class for all MariaDB-compatible monitor server objects.
 */
class MariaServer : public MonitorServer
{
public:
    MariaServer(SERVER* server, const SharedSettings& shared);
    ~MariaServer();

    /**
     * Ping or connect to a database. If connection does not exist or ping fails, a new connection
     * is created. This will always leave a valid database handle in @c *ppCon, allowing the user
     * to call MySQL C API functions to find out the reason of the failure. Also measures server ping.
     *
     * @param sett        Connection settings
     * @param pServer     A server
     * @param ppConn      Address of pointer to a MYSQL instance. The instance should either be
     *                    valid or NULL.
     * @param pError      Pointer where the error message is stored
     *
     * @return Connection status.
     */
    static ConnectResult
    ping_or_connect_to_db(const ConnectionSettings& sett, SERVER& server, MYSQL** ppConn,
                          std::string* pError);

    /**
     * Execute a query which returns data.
     *
     * @param query The query
     * @param errmsg_out Where to store an error message if query fails. Can be null.
     * @param errno_out Error code output. Can be null.
     * @return Pointer to query results, or an empty pointer on failure
     */
    std::unique_ptr<mxb::QueryResult>
    execute_query(const std::string& query, std::string* errmsg_out = nullptr,
                  unsigned int* errno_out = nullptr);

    ConnectResult ping_or_connect() override;
    void          close_conn() override;
    void          fetch_uptime() override;
    void          check_permissions() override;
    void          update_disk_space_status() override;

    MYSQL* con {nullptr};   /**< The MySQL connection */

private:
    bool                       fetch_variables() override;
    virtual const std::string& permission_test_query() const;
};
}
