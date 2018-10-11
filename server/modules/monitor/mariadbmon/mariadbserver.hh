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
#include "mariadbmon_common.hh"
#include <functional>
#include <string>
#include <memory>
#include <maxscale/monitor.h>
#include <maxbase/stopwatch.hh>
#include "server_utils.hh"

class QueryResult;
class MariaDBServer;
// Server pointer array
typedef std::vector<MariaDBServer*> ServerArray;

/**
 * Data required for checking replication topology cycles and other graph algorithms. This data is mostly
 * used by the monitor object, as the data only makes sense in relation to other nodes.
 */
struct NodeData
{
    // Default values for index parameters
    static const int INDEX_NOT_VISITED = 0;
    static const int INDEX_FIRST = 1;
    // Default values for the cycle
    static const int CYCLE_NONE = 0;
    static const int CYCLE_FIRST = 1;
    // Default value for reach
    static const int REACH_UNKNOWN = -1;

    // Bookkeeping for graph searches. May be overwritten by multiple algorithms.
    int  index;         /* Marks the order in which this node was visited. */
    int  lowest_index;  /* The lowest index node this node has in its subtree. */
    bool in_stack;      /* Is this node currently is the search stack. */

    // Results from algorithm runs. Should only be overwritten when server data has been queried.
    int         cycle;                      /* Which cycle is this node part of, if any. */
    int         reach;                      /* How many servers replicate from this server or its children. */
    ServerArray parents;                    /* Which nodes is this node replicating from. External masters
                                             * excluded. */
    ServerArray          children;          /* Which nodes are replicating from this node. */
    std::vector<int64_t> external_masters;  /* Server id:s of external masters. */

    NodeData();

    /**
     * Reset result data to default values. Should be ran when starting an iteration.
     */
    void reset_results();

    /**
     * Reset index data. Should be ran before an algorithm run.
     */
    void reset_indexes();
};

/**
 * Monitor specific information about a server. Eventually, this will be the primary data structure handled
 * by the monitor. These are initialized in @c init_server_info.
 */
class MariaDBServer
{
public:
    enum class version
    {
        UNKNOWN,            /* Totally unknown. Server has not been connected to yet. */
        OLD,                /* Anything older than 5.5. These are no longer supported by the monitor. */
        MARIADB_MYSQL_55,   /* MariaDB 5.5 series or MySQL 5.5 and later. Does not have gtid (on MariaDB) so
                             * all gtid-related features (failover etc.) are disabled. */
        MARIADB_100,        /* MariaDB 10.0 and greater. In practice though, 10.0.2 or greater is assumed.
                             * All monitor features are on. */
        BINLOG_ROUTER       /* MaxScale binlog server. Requires special handling. */
    };

    enum class BinlogMode
    {
        BINLOG_ON,
        BINLOG_OFF
    };

    // This class groups some miscellaneous replication related settings together.
    class ReplicationSettings
    {
    public:
        bool gtid_strict_mode = false;  /* Enable additional checks for replication */
        bool log_bin = false;           /* Is binary logging enabled? */
        bool log_slave_updates = false; /* Does the slave write replicated events to binlog? */
    };

    /* Monitored server base class/struct. MariaDBServer does not own the struct, it is not freed
     * (or connection closed) when a MariaDBServer is destroyed. */
    MXS_MONITORED_SERVER* m_server_base = NULL;
    /* What position this server has in the monitor config? Used for tiebreaking between servers. */
    int m_config_index = 0;

    version m_version = version::UNKNOWN;           /* Server version/type. */
    int64_t m_server_id = SERVER_ID_UNKNOWN;        /* Value of @@server_id. Valid values are
                                                     * 32bit unsigned. */
    int64_t m_gtid_domain_id = GTID_DOMAIN_UNKNOWN; /* The value of gtid_domain_id, the domain which is used
                                                     * for new non-replicated events. */

    bool             m_read_only = false;   /* Value of @@read_only */
    GtidList         m_gtid_current_pos;    /* Gtid of latest event. */
    GtidList         m_gtid_binlog_pos;     /* Gtid of latest event written to binlog. */
    SlaveStatusArray m_slave_status;        /* Data returned from SHOW (ALL) SLAVE(S) STATUS */
    NodeData         m_node;                /* Replication topology data */

    /* Replication lag of the server. Used during calculation so that the actual SERVER struct is
     * only written to once. */
    int m_replication_lag = MXS_RLAG_UNDEFINED;
    /* Has anything that could affect replication topology changed this iteration?
     * Causes: server id, slave connections, read-only. */
    bool m_topology_changed = true;
    /* Miscellaneous replication related settings. These are not normally queried from the server, call
     * 'update_replication_settings' before use. */
    ReplicationSettings m_rpl_settings;

    bool m_print_update_errormsg = true;    /* Should an update error be printed? */

    MariaDBServer(MXS_MONITORED_SERVER* monitored_server, int config_index);

    /**
     * Print server information to a json object.
     *
     * @return Json diagnostics object
     */
    json_t* to_json() const;

    /**
     * Print server information to a string.
     *
     * @return Diagnostics string
     */
    std::string diagnostics() const;

    /**
     * Query this server.
     */
    void monitor_server();

    /**
     * Update server version. This method should be called after (re)connecting to a
     * backend. Calling this every monitoring loop is overkill.
     */
    void update_server_version();

    /**
     * Checks monitor permissions on the server. Sets/clears the SERVER_AUTH_ERROR bit.
     */
    void check_permissions();

    /**
     * Calculate how many events are left in the relay log of the slave connection.
     *
     * @param slave_conn The slave connection to calculate for
     * @return Number of events in relay log. Always  0 or greater.
     */
    uint64_t relay_log_events(const SlaveStatus& slave_conn);

    /**
     * Execute a query which returns data. The results are returned as a unique pointer to a QueryResult
     * object. The column names of the results are assumed unique.
     *
     * @param query The query
     * @param errmsg_out Where to store an error message if query fails. Can be null.
     * @return Pointer to query results, or an empty pointer on failure
     */
    std::unique_ptr<QueryResult> execute_query(const std::string& query, std::string* errmsg_out = NULL);

    /**
     * execute_cmd_ex with query retry ON.
     */
    bool execute_cmd(const std::string& cmd, std::string* errmsg_out = NULL);

    /**
     * execute_cmd_ex with query retry OFF.
     */
    bool execute_cmd_no_retry(const std::string& cmd,
                              std::string* errmsg_out = NULL, unsigned int* errno_out = NULL);

    /**
     * Update server slave connection information.
     *
     * @param gtid_domain Which gtid domain should be parsed.
     * @param errmsg_out Where to store an error message if query fails. Can be null.
     * @return True on success
     */
    bool do_show_slave_status(std::string* errmsg_out = NULL);

    /**
     * Query gtid_current_pos and gtid_binlog_pos and save the values to the server.
     *
     * @param errmsg_out Where to store an error message if query fails. Can be null.
     * @return True if successful
     */
    bool update_gtids(std::string* errmsg_out = NULL);

    /**
     * Query a few miscellaneous replication settings.
     *
     * @param error_out Query error output
     * @return True on success
     */
    bool update_replication_settings(std::string* error_out = NULL);

    /**
     * Query and save server_id, read_only and (if 10.X) gtid_domain_id.
     *
     * @param errmsg_out Where to store an error message if query fails. Can be null.
     * @return True on success.
     */
    bool read_server_variables(std::string* errmsg_out = NULL);

    /**
     * Print warnings if gtid_strict_mode or log_slave_updates is off. Does not query the server,
     * so 'update_replication_settings' should have been called recently to update the values.
     */
    void warn_replication_settings() const;

    /**
     * Wait until server catches up to demotion target. Only considers gtid domains common
     * to this server and the target. The gtid compared to on the demotion target is 'gtid_binlog_pos'.
     * It is not updated during this method.
     *
     * The gtid used for comparison on this server is 'gtid_binlog_pos' if this server has both 'log_bin'
     * and 'log_slave_updates' on, and 'gtid_current_pos' otherwise. This server is updated during the
     * method.
     *
     * @return True, if target server gtid was reached within allotted time
     */
    bool catchup_to_master(ClusterOperation& op);

    /**
     * Find slave connection to the target server. If the IO thread is trying to connect
     * ("Connecting"), the connection is only accepted if the 'Master_Server_Id' is known to be correct.
     * If the IO or the SQL thread is stopped, the connection is not returned.
     *
     * @param target Immediate master or relay server
     * @return The slave status info of the slave thread, or NULL if not found or not accepted
     */
    const SlaveStatus* slave_connection_status(const MariaDBServer* target) const;

    /**
     * Find slave connection to the target server. Only considers host and port when selecting
     * the connection. A matching connection is accepted even if its IO or SQL thread is stopped.
     *
     * @param target Immediate master or relay server
     * @return The slave status info of the slave thread, or NULL if not found
     */
    const SlaveStatus* slave_connection_status_host_port(const MariaDBServer* target) const;

    /**
     * Checks if this server can replicate from master. Only considers gtid:s and only detects obvious errors.
     * The non-detected errors will mostly be detected once the slave tries to start replicating.
     *
     * @param master_info Master server
     * @param error_out Details the reason for a negative result
     * @return True if slave can replicate from master
     */
    bool can_replicate_from(MariaDBServer* master, std::string* error_out);

    /**
     * Redirect one slave server to another master
     *
     * @param change_cmd Change master command, usually generated by generate_change_master_cmd()
     * @return True if slave accepted all commands
     */
    bool redirect_one_slave(const std::string& change_cmd);

    /**
     * Joins this standalone server to the cluster.
     *
     * @param change_cmd Change master command
     * @param disable_server_events Should events be disabled on the server
     * @return True if commands were accepted by server
     */
    bool join_cluster(const std::string& change_cmd, bool disable_server_events);

    /**
     * Check if the server can be demoted by switchover.
     *
     * @param reason_out Output explaining why server cannot be demoted
     * @return True if server can be demoted
     */
    bool can_be_demoted_switchover(std::string* reason_out);

    /**
     * Check if the server can be demoted by failover.
     *
     * @param operation Switchover or failover
     * @param reason_out Output explaining why server cannot be demoted
     * @return True if server can be demoted
     */
    bool can_be_demoted_failover(std::string* reason_out);

    /**
     * Check if the server can be promoted by switchover or failover.
     *
     * @param op Switchover or failover
     * @param demotion_target The server this should be promoted to
     * @param reason_out Output for the reason server cannot be promoted
     * @return True, if suggested new master is a viable promotion candidate
     */
    bool can_be_promoted(OperationType op, const MariaDBServer* demotion_target, std::string* reason_out);

    /**
     * Read the file contents and send them as sql queries to the server. Any data
     * returned by the queries is discarded.
     *
     * @param server Server to send queries to
     * @param path Text file path.
     * @param error_out Error output
     * @return True if file was read and all commands were completed successfully
     */
    bool run_sql_from_file(const std::string& path, json_t** error_out);

    /**
     * Enable any "SLAVESIDE_DISABLED" events. Event scheduler is not touched.
     *
     * @param error_out Error output
     * @return True if all SLAVESIDE_DISABLED events were enabled
     */
    bool enable_events(json_t** error_out);

    /**
     * Disable any "ENABLED" events. Event scheduler is not touched.
     *
     * @param binlog_mode If OFF, binlog event creation is disabled for the session during method execution.
     * @param error_out Error output
     * @return True if all ENABLED events were disabled
     */
    bool disable_events(BinlogMode binlog_mode, json_t** error_out);

    /**
     * Stop and delete all slave connections.
     *
     * @param error_out Error output
     * @return True if successful. If false, some connections may have been successfully deleted.
     */
    bool reset_all_slave_conns(json_t** error_out);

    /**
     * Promote this server to take role of demotion target. Remove slave connections from this server.
     * If target is/was a master, set read-only to OFF. Copy slave connections from target.
     *
     * @param op Cluster operation descriptor
     * @return True if successful
     */
    bool promote(ClusterOperation& op);

    /**
     * Demote this server. Removes all slave connections. If server was master, sets read_only.
     *
     * @param op Cluster operation descriptor
     * @return True if successful
     */
    bool demote(ClusterOperation& op);

    /**
     * Redirect the slave connection going to old master to replicate from new master.
     *
     * @param op Operation descriptor
     * @param old_master The connection to this server is redirected
     * @param new_master The new master for the redirected connection
     * @return True on success
     */
    bool redirect_existing_slave_conn(ClusterOperation& op, const MariaDBServer* old_master,
                                      const MariaDBServer* new_master);

    /**
     * Copy slave connections to this server. This is usually needed during switchover promotion and on
     * the demoted server. It is assumed that all slave connections of this server have
     * been stopped & removed so there will be no conflicts with existing connections.
     * A special rule is applied to a connection which points to this server itself: that connection
     * is directed towards the 'replacement'. This is required to properly handle connections between
     * the promotion and demotion targets.
     *
     * @param op Operation descriptor
     * @params conns_to_copy The connections to add to the server
     * @params replacement Which server should rep
     * @return True on success
     */
    bool copy_slave_conns(ClusterOperation& op, const SlaveStatusArray& conns_to_copy,
                          const MariaDBServer* replacement);

    /**
     * Is binary log on? 'update_replication_settings' should be ran before this function to query the data.
     *
     * @return True if server has binary log enabled
     */
    bool binlog_on() const;

    /**
     * Check if server is a running master.
     *
     * @return True if server is a master
     */
    bool is_master() const;

    /**
     * Check if server is a running slave.
     *
     * @return True if server is a slave
     */
    bool is_slave() const;

    /**
     * Check if server is a slave of an external server.
     *
     * @return True if server is a slave of an external server
     */
    bool is_slave_of_ext_master() const;

    /**
     * Check if server is running and not in maintenance.
     *
     * @return True if server is usable
     */
    bool is_usable() const;

    /**
     * Check if server is running.
     *
     * @return True if server is running
     */
    bool is_running() const;

    /**
     * Check if server is down.
     *
     * @return True if server is down
     */
    bool is_down() const;

    /**
     * Check if server is in maintenance.
     */
    bool is_in_maintenance() const;

    /**
     * Is the server a relay master.
     */
    bool is_relay_master() const;

    /**
     * Is the server low on disk space?
     */
    bool is_low_on_disk_space() const;

    /**
     * Check if server has the given bits on in 'pending_status'.
     *
     * @param bits Bits to check
     * @return True if all given bits are on
     */
    bool has_status(uint64_t bits) const;

    /**
     * Check if server has the given bits on in 'mon_prev_status'.
     *
     * @param bits Bits to check
     * @return True if all given bits are on
     */
    bool had_status(uint64_t bits) const;

    /**
     * Getter for m_read_only.
     *
     * @return True if server is in read_only mode
     */
    bool is_read_only() const;

    /**
     * Returns the server name.
     *
     * @return Server unique name
     */
    const char* name() const;

    /**
     * Clear server pending status flags.
     *
     * @param bits Which flags to clear
     */
    void clear_status(uint64_t bits);

    /**
     * Set server pending status flags.
     *
     * @param bits Which flags to set
     */
    void set_status(uint64_t bits);

private:
    class EventInfo;
    typedef std::function<void (const EventInfo&, json_t** error_out)> ManipulatorFunc;

    enum class StopMode
    {
        STOP_ONLY,
        RESET,
        RESET_ALL
    };
    enum class QueryRetryMode
    {
        ENABLED,
        DISABLED
    };
    enum class ReadOnlySetting
    {
        ENABLE,
        DISABLE
    };

    bool               update_slave_status(std::string* errmsg_out = NULL);
    bool               sstatus_array_topology_equal(const SlaveStatusArray& new_slave_status);
    const SlaveStatus* sstatus_find_previous_row(const SlaveStatus& new_row, size_t guess);

    void warn_event_scheduler();
    bool events_foreach(ManipulatorFunc& func, json_t** error_out);
    bool alter_event(const EventInfo& event, const std::string& target_status,
                     json_t** error_out);

    bool stop_slave_conn(const std::string& conn_name, StopMode mode, maxbase::Duration time_limit,
                         json_t** error_out);

    bool remove_slave_conns(ClusterOperation& op, const SlaveStatusArray& conns_to_remove);
    bool execute_cmd_ex(const std::string& cmd, QueryRetryMode mode,
                        std::string* errmsg_out = NULL, unsigned int* errno_out = NULL);

    bool execute_cmd_time_limit(const std::string& cmd, maxbase::Duration time_limit,
                                std::string* errmsg_out);

    bool        set_read_only(ReadOnlySetting value, maxbase::Duration time_limit, json_t** error_out);
    bool        merge_slave_conns(ClusterOperation& op, const SlaveStatusArray& conns_to_merge);
    bool        create_start_slave(ClusterOperation& op, const SlaveStatus& slave_conn);
    std::string generate_change_master_cmd(ClusterOperation& op, const SlaveStatus& slave_conn);
};
