#pragma once

/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include <maxscale/cppdefs.hh>
#include <string>
#include <vector>

#include <maxscale/monitor.h>

/** Utility macro for printing both MXS_ERROR and json error */
#define PRINT_MXS_JSON_ERROR(err_out, format, ...)\
    do {\
       MXS_ERROR(format, ##__VA_ARGS__);\
       if (err_out)\
       {\
            *err_out = mxs_json_error_append(*err_out, format, ##__VA_ARGS__);\
       }\
    } while (false)

using std::string;

enum mysql_server_version
{
    MYSQL_SERVER_VERSION_100,
    MYSQL_SERVER_VERSION_55,
    MYSQL_SERVER_VERSION_51
};

extern const int64_t SERVER_ID_UNKNOWN;

typedef std::vector<string> StringVector;
typedef std::vector<MXS_MONITORED_SERVER*> ServerVector;

class Gtid
{
public:
    uint32_t domain;
    int64_t server_id; // Is actually 32bit unsigned. 0 is only used by server versions  <= 10.1
    uint64_t sequence;
    Gtid();

    /**
     * Parse a Gtid-triplet from a string. In case of a multi-triplet value, only the triplet with
     * the given domain is returned.
     *
     * @param str Gtid string
     * @param search_domain The Gtid domain whose triplet should be returned. Negative domain stands for
     * autoselect, which is only allowed when the string contains one triplet.
     */
    Gtid(const char* str, int64_t search_domain = -1);

    bool operator == (const Gtid& rhs) const;

    string to_string() const;
private:
    void parse_triplet(const char* str);
};

// Contains data returned by one row of SHOW ALL SLAVES STATUS
class SlaveStatusInfo
{
public:
    int64_t master_server_id;       /**< The master's server_id value. Valid ids are 32bit unsigned. -1 is
                                     *   unread/error. */
    string master_host;             /**< Master server host name. */
    int master_port;                /**< Master server port. */
    bool slave_io_running;          /**< Whether the slave I/O thread is running and connected. */
    bool slave_sql_running;         /**< Whether or not the SQL thread is running. */
    string master_log_file;         /**< Name of the master binary log file that the I/O thread is currently
                                     *   reading from. */
    uint64_t read_master_log_pos;   /**< Position up to which the I/O thread has read in the current master
                                     *   binary log file. */
    Gtid gtid_io_pos;               /**< Gtid I/O position of the slave thread. Only shows the triplet with
                                     *   the current master domain. */
    string last_error;              /**< Last IO or SQL error encountered. */

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
 * Monitor specific information about a server
 *
 * Note: These are initialized in @c init_server_info
 */
class MySqlServerInfo
{
public:
    int64_t          server_id;             /**< Value of @@server_id. Valid values are 32bit unsigned. */
    int              group;                 /**< Multi-master group where this server belongs,
                                             *   0 for servers not in groups */
    bool             read_only;             /**< Value of @@read_only */
    bool             slave_configured;      /**< Whether SHOW SLAVE STATUS returned rows */
    bool             binlog_relay;          /** Server is a Binlog Relay */
    int              n_slaves_configured;   /**< Number of configured slave connections*/
    int              n_slaves_running;      /**< Number of running slave connections */
    int              slave_heartbeats;      /**< Number of received heartbeats */
    double           heartbeat_period;      /**< The time interval between heartbeats */
    time_t           latest_event;          /**< Time when latest event was received from the master */
    int64_t          gtid_domain_id;        /**< The value of gtid_domain_id, the domain which is used for
                                             *   new non-replicated events. */
    Gtid             gtid_current_pos;      /**< Gtid of latest event. Only shows the triplet
                                             *   with the current master domain. */
    Gtid             gtid_binlog_pos;       /**< Gtid of latest event written to binlog. Only shows
                                             *   the triplet with the current master domain. */
    SlaveStatusInfo  slave_status;          /**< Data returned from SHOW SLAVE STATUS */
    ReplicationSettings rpl_settings;       /**< Miscellaneous replication related settings */
    mysql_server_version version;           /**< Server version, 10.X, 5.5 or 5.1 */

    MySqlServerInfo();

    /**
     * Calculate how many events are left in the relay log. If gtid_current_pos is ahead of Gtid_IO_Pos,
     * or a server_id is unknown, an error value is returned.
     *
     * @return Number of events in relay log according to latest queried info. A negative value signifies
     * an error in the gtid-values.
     */
    int64_t relay_log_events();
};

/**
 * Scan a server id from a string.
 *
 * @param id_string
 * @return Server id, or -1 if scanning fails
 */
int64_t scan_server_id(const char* id_string);

/**
 * Generates a MASTER_GTID_WAIT()- query.
 * @param gtid What gtid to wait for
 * @param timeout Maximum wait time in seconds
 * @return The query
 */
string generate_master_gtid_wait_cmd(const Gtid& gtid, double timeout);

/**
 * Query one row of results, save strings to array. Any additional rows are ignored.
 *
 * @param database The database to query.
 * @param query The query to execute.
 * @param expected_cols How many columns the result should have.
 * @param output The output array to populate.
 * @return True on success.
 */
bool query_one_row(MXS_MONITORED_SERVER *database, const char* query, unsigned int expected_cols,
                   StringVector* output);

/**
 * Get MariaDB connection error strings from all the given servers, form one string.
 *
 * @param slaves Servers with errors
 * @return Concatenated string.
 */
string get_connection_errors(const ServerVector& servers);

/**
 * Generates a list of server names separated by ', '
 *
 * @param array The servers
 * @return Server names
 */
string monitored_servers_to_string(const ServerVector& array);
