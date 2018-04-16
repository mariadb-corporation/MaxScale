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
#include "mariadbmon_common.hh"
#include <string>
#include <memory>
#include <maxscale/monitor.h>
#include "gtid.hh"

enum mysql_server_version
{
    MYSQL_SERVER_VERSION_100,
    MYSQL_SERVER_VERSION_55,
    MYSQL_SERVER_VERSION_51
};

enum print_repl_warnings_t
{
    WARNINGS_ON,
    WARNINGS_OFF
};

class QueryResult;

// Contains data returned by one row of SHOW ALL SLAVES STATUS
class SlaveStatusInfo
{
public:
    int64_t master_server_id;       /**< The master's server_id value. Valid ids are 32bit unsigned. -1 is
                                     *   unread/error. */
    std::string master_host;        /**< Master server host name. */
    int master_port;                /**< Master server port. */
    bool slave_io_running;          /**< Whether the slave I/O thread is running and connected. */
    bool slave_sql_running;         /**< Whether or not the SQL thread is running. */
    std::string master_log_file;    /**< Name of the master binary log file that the I/O thread is currently
                                     *   reading from. */
    uint64_t read_master_log_pos;   /**< Position up to which the I/O thread has read in the current master
                                     *   binary log file. */
    GtidList gtid_io_pos;           /**< Gtid I/O position of the slave thread. */
    std::string last_error;         /**< Last IO or SQL error encountered. */

    SlaveStatusInfo();
};

// This class groups some miscellaneous replication related settings together.
class ReplicationSettings
{
public:
    bool gtid_strict_mode;      /**< Enable additional checks for replication */
    bool log_bin;               /**< Is binary logging enabled */
    bool log_slave_updates;     /**< Does the slave log replicated events to binlog */
    ReplicationSettings()
        : gtid_strict_mode(false)
        , log_bin(false)
        , log_slave_updates(false)
    {}
};

/**
 * Monitor specific information about a server. Eventually, this will be the primary data structure handled
 * by the monitor. These are initialized in @c init_server_info.
 */
class MariaDBServer
{
public:
    MXS_MONITORED_SERVER* server_base;      /**< Monitored server base class/struct. MariaDBServer does not
                                              *  own the struct, it is not freed (or connection closed) when
                                              *  a MariaDBServer is destroyed. Can be const on gcc 4.8 */
    mysql_server_version version;           /**< Server version, 10.X, 5.5 or 5.1 */
    int64_t         server_id;              /**< Value of @@server_id. Valid values are 32bit unsigned. */
    int             group;                  /**< Multi-master group where this server belongs,
                                              *  0 for servers not in groups */
    bool            read_only;              /**< Value of @@read_only */
    bool            slave_configured;       /**< Whether SHOW SLAVE STATUS returned rows */
    bool            binlog_relay;           /** Server is a Binlog Relay */
    int             n_slaves_configured;    /**< Number of configured slave connections*/
    int             n_slaves_running;       /**< Number of running slave connections */
    int             slave_heartbeats;       /**< Number of received heartbeats */
    double          heartbeat_period;       /**< The time interval between heartbeats */
    time_t          latest_event;           /**< Time when latest event was received from the master */
    int64_t         gtid_domain_id;         /**< The value of gtid_domain_id, the domain which is used for
                                              *  new non-replicated events. */
    GtidList        gtid_current_pos;       /**< Gtid of latest event. */
    GtidList        gtid_binlog_pos;        /**< Gtid of latest event written to binlog. */
    SlaveStatusInfo slave_status;           /**< Data returned from SHOW SLAVE STATUS */
    ReplicationSettings rpl_settings;       /**< Miscellaneous replication related settings */

    MariaDBServer(MXS_MONITORED_SERVER* monitored_server);

    /**
     * Calculate how many events are left in the relay log.
     *
     * @return Number of events in relay log according to latest queried info.
     */
    int64_t relay_log_events();

    /**
     * Execute a query which returns data. The results are returned as an auto-pointer to a QueryResult
     * object.
     *
     * @param query The query
     * @return Pointer to query results, or an empty auto-ptr on failure. Currently, the column names of the
     * results are assumed unique.
     */
    std::auto_ptr<QueryResult> execute_query(const std::string& query);

    /**
     * Update server slave connection information.
     *
     * @param gtid_domain Which gtid domain should be parsed.
     * @return True on success
     */
    bool do_show_slave_status();

    /**
     * Query gtid_current_pos and gtid_binlog_pos and save the values to the server.
     *
     * @return True if successful
     */
    bool update_gtids();

    /**
     * Query a few miscellaneous replication settings.
     *
     * @return True on success
     */
    bool update_replication_settings();

    /**
     * Query and save server_id, read_only and (if 10.X) gtid_domain_id.
     */
    void read_server_variables();

    /**
     * Check if server has binary log enabled. Print warnings if gtid_strict_mode or log_slave_updates is off.
     *
     * @param print_on Print warnings or not
     * @return True if log_bin is on
     */
    bool check_replication_settings(print_repl_warnings_t print_warnings = WARNINGS_ON);

    /**
     * Wait until server catches up to the target gtid. Only considers gtid domains common to this server
     * and the target gtid. The gtid compared is the gtid_binlog_pos if this server has both log_bin and
     * log_slave_updates on, and gtid_current_pos otherwise.
     *
     * @param target Which gtid must be reached
     * @param timeout Maximum wait time in seconds
     * @param err_out json object for error printing. Can be NULL.
     * @return True, if target gtid was reached within allotted time
     */
    bool wait_until_gtid(const GtidList& target, int timeout, json_t** err_out);

    /**
     * Convenience method for SERVER_IS_MASTER
     *
     * @return True if server is a master
     */
    bool is_master() const;

    /**
     * Convenience method for SERVER_IS_SLAVE
     *
     * @return True if server is a slave
     */
    bool is_slave() const;

    /**
     * Convenience method for SERVER_IS_RUNNING
     *
     * @return True if server is running
     */
    bool is_running() const;

    /**
     * Convenience method for SERVER_IS_DOWN
     *
     * @return True if server is down
     */
    bool is_down() const;

    /**
     * Returns the server name.
     *
     * @return Server unique name
     */
    const char* name() const;

    /**
     * Print server information to a json object.
     *
     * @param multimaster Print multimaster group
     * @return Json diagnostics object
     */
    json_t* diagnostics_json(bool multimaster) const;

    /**
     * Print server information to a string.
     *
     * @param multimaster Print multimaster group
     * @return Diagnostics string
     */
    std::string diagnostics(bool multimaster) const;

    /**
     * Check if server is using gtid replication.
     *
     * @param error_out Error output
     * @return True if using gtid-replication. False if not, or if server is not a slave or otherwise does
     * not have a gtid_IO_Pos.
     */
    bool uses_gtid(json_t** error_out);

    /**
     * Update replication settings, gtid:s and slave status of the server.
     *
     * @param server Slave to update
     * @return True on success. False on error, or if server is not a slave (slave SQL not running).
     */
    bool update_slave_info();

    /**
     * Checks if this server can replicate from master. Only considers gtid:s and only detects obvious errors.
     * The non-detected errors will mostly be detected once the slave tries to start replicating.
     *
     * @param master_info Master server
     * @return True if slave can replicate from master
     */
    bool can_replicate_from(MariaDBServer* master);

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
     * @return True if commands were accepted by server
     */
    bool join_cluster(const std::string& change_cmd);

    /**
     * Waits until this server has processed all its relay log, or time is up.
     *
     * @param seconds_remaining How much time left
     * @param err_out Json error output
     * @return True if relay log was processed within time limit, or false if time ran out
     * or an error occurred.
     */
    bool failover_wait_relay_log(int seconds_remaining, json_t** err_out);
};

/**
 * Helper class for simplifying working with resultsets. Used in MariaDBServer.
 */
class QueryResult
{
    // These need to be banned to avoid premature destruction.
    QueryResult(const QueryResult&) = delete;
    QueryResult& operator = (const QueryResult&) = delete;

public:
    QueryResult(MYSQL_RES* resultset = NULL);
    ~QueryResult();

    /**
     * Advance to next row. Affects all result returning functions.
     *
     * @return True if the next row has data, false if the current row was the last one.
     */
    bool next_row();

    /**
     * Get the index of the current row.
     *
     * @return Current row index, or -1 if no data or next_row() has not been called yet.
     */
    int64_t get_row_index() const;

    /**
     * How many columns the result set has.
     *
     * @return Column count, or -1 if no data.
     */
    int64_t get_column_count() const;

    /**
     * Get a numeric index for a column name. May give wrong results if column names are not unique.
     *
     * @param col_name Column name
     * @return Index or -1 if not found.
     */
    int64_t get_col_index(const std::string& col_name) const;

    /**
     * Read a string value from the current row and given column. Empty string and (null) are both interpreted
     * as the empty string.
     *
     * @param column_ind Column index
     * @return Value as string
     */
    std::string get_string(int64_t column_ind) const;

    /**
     * Read a non-negative integer value from the current row and given column.
     *
     * @param column_ind Column index
     * @return Value as integer. 0 or greater indicates success, -1 is returned on error.
     */
    int64_t get_uint(int64_t column_ind) const;

    /**
     * Read a boolean value from the current row and given column.
     *
     * @param column_ind Column index
     * @return Value as boolean. Returns true if the text is either 'Y' or '1'.
     */
    bool get_bool(int64_t column_ind) const;

private:
    MYSQL_RES* m_resultset; // Underlying result set, freed at dtor.
    std::tr1::unordered_map<std::string, int64_t> m_col_indexes; // Map of column name -> index
    int64_t m_columns;     // How many columns does the data have. Usually equal to column index map size.
    MYSQL_ROW m_rowdata;   // Data for current row
    int64_t m_current_row; // Index of current row
};
