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

#include <string>
#include <vector>
#include <maxbase/stopwatch.hh>
#include <maxscale/mysql_utils.h>

class MariaDBServer;

/**
 * Class which encapsulates a gtid (one domain-server_id-sequence combination)
 */
class Gtid
{
public:

    /**
     * Constructs an invalid Gtid.
     */
    Gtid();

    /**
     * Constructs a gtid with given values. The values are not checked.
     *
     * @param domain Domain
     * @param server_id Server id
     * @param sequence Sequence
     */
    Gtid(uint32_t domain, int64_t server_id, uint64_t sequence);

    /**
     * Parse one gtid from null-terminated string. Handles multi-domain gtid:s properly. Should be called
     * repeatedly for a multi-domain gtid string by giving the value of @c endptr as @c str.
     *
     * @param str First number of a gtid in a gtid-string
     * @param endptr A pointer to save the position at after the last parsed character.
     * @return A new gtid. If an error occurs, the server_id of the returned triplet is -1.
     */
    static Gtid from_string(const char* str, char** endptr);

    bool eq(const Gtid& rhs) const;

    std::string to_string() const;

    /**
     * Comparator, used when sorting by domain id.
     *
     * @param lhs Left side
     * @param rhs Right side
     * @return True if lhs should be before rhs
     */
    static bool compare_domains(const Gtid& lhs, const Gtid& rhs)
    {
        return lhs.m_domain < rhs.m_domain;
    }

    uint32_t m_domain;
    int64_t  m_server_id;   // Valid values are 32bit unsigned. 0 is only used by server versions  <= 10.1
    uint64_t m_sequence;
};

inline bool operator==(const Gtid& lhs, const Gtid& rhs)
{
    return lhs.eq(rhs);
}

/**
 * Class which encapsulates a list of gtid:s (e.g. 1-2-3,2-2-4). Server variables such as gtid_binlog_pos
 * are GtidLists. */
class GtidList
{
public:

    // Used with events_ahead()
    enum substraction_mode_t
    {
        MISSING_DOMAIN_IGNORE,
        MISSING_DOMAIN_LHS_ADD
    };

    /**
     * Parse the gtid string and return an object. Orders the triplets by domain id.
     *
     * @param gtid_string gtid as given by server. String must not be empty.
     * @return The parsed (possibly multidomain) gtid. In case of error, the gtid will be empty.
     */
    static GtidList from_string(const std::string& gtid_string);

    /**
     * Return a string version of the gtid list.
     *
     * @return A string similar in form to how the server displays gtid:s
     */
    std::string to_string() const;

    /**
     * Check if a server with this gtid can replicate from a master with a given gtid. Only considers
     * gtid:s and only detects obvious errors. The non-detected errors will mostly be detected once
     * the slave tries to start replicating.
     *
     * TODO: Add support for Replicate_Do/Ignore_Id:s
     *
     * @param master_gtid Master server gtid
     * @return True if replication looks possible
     */
    bool can_replicate_from(const GtidList& master_gtid);

    /**
     * Is the gtid empty.
     *
     * @return True if gtid has 0 triplets
     */
    bool empty() const;

    /**
     * Full comparison.
     *
     * @param rhs Other gtid
     * @return True if both gtid:s have identical triplets or both are empty
     */
    bool operator==(const GtidList& rhs) const;

    /**
     * Calculate the number of events this GtidList is ahead of the given GtidList. The
     * result is always 0 or greater: if a sequence number of a domain on rhs is greater than on the same
     * domain on the calling GtidList, the sequences are considered identical. Missing domains are
     * handled depending on the value of @c domain_substraction_mode.
     *
     * @param rhs The value doing the substracting
     * @param domain_substraction_mode How domains that exist on the caller but not on @c rhs are handled.
     * If MISSING_DOMAIN_IGNORE, these are simply ignored. If MISSING_DOMAIN_LHS_ADD,
     * the sequence number on lhs is added to the total difference.
     * @return The number of events between the two gtid:s
     */
    uint64_t events_ahead(const GtidList& rhs, substraction_mode_t domain_substraction_mode) const;

    /**
     * Return an individual gtid with the given domain.
     *
     * @param domain Which domain to search for
     * @return The gtid within the list. If domain is not found, an invalid gtid is returned.
     */
    Gtid get_gtid(uint32_t domain) const;

private:
    std::vector<Gtid> m_triplets;
};

// Contains data returned by one row of SHOW ALL SLAVES STATUS
class SlaveStatus
{
public:
    enum slave_io_running_t
    {
        SLAVE_IO_YES,
        SLAVE_IO_CONNECTING,
        SLAVE_IO_NO,
    };

    std::string owning_server;                              /* Server name of the owner */
    bool        seen_connected = false;                     /* Has this slave connection been seen connected,
                                                             * meaning that the master server id
                                                             * is correct? */
    std::string name;                                       /* Slave connection name. Must be unique for
                                                             * the server.*/
    int64_t master_server_id = SERVER_ID_UNKNOWN;           /* The master's server_id value. Valid ids are
                                                             * 32bit unsigned. -1 is unread/error. */
    std::string        master_host;                         /* Master server host name. */
    int                master_port = PORT_UNKNOWN;          /* Master server port. */
    slave_io_running_t slave_io_running = SLAVE_IO_NO;      /* Slave I/O thread running state: * "Yes",
                                                             * "Connecting" or "No" */
    bool slave_sql_running = false;                         /* Slave SQL thread running state, true if "Yes"
                                                             * */
    GtidList    gtid_io_pos;                                /* Gtid I/O position of the slave thread. */
    std::string last_error;                                 /* Last IO or SQL error encountered. */
    int         seconds_behind_master = MXS_RLAG_UNDEFINED; /* How much behind the slave is. */
    int64_t     received_heartbeats = 0;                    /* How many heartbeats the connection has received
                                                             * */

    /* Time of the latest gtid event or heartbeat the slave connection has received, timed by the monitor. */
    maxbase::Clock::time_point last_data_time = maxbase::Clock::now();


    std::string to_string() const;
    json_t*     to_json() const;

    /**
     * Create a short description in the form of "Slave connection from <slave> to <master>"
     *
     * @return Description
     */
    std::string to_short_string() const;

    static slave_io_running_t slave_io_from_string(const std::string& str);
    static std::string        slave_io_to_string(slave_io_running_t slave_io);
    bool                      should_be_copied(std::string* ignore_reason_out) const;
};

typedef std::vector<SlaveStatus> SlaveStatusArray;

enum class OperationType
{
    SWITCHOVER,
    FAILOVER
};

/**
 *  Class which encapsulates many settings and status descriptors for a failover/switchover.
 *  Is more convenient to pass around than the separate elements. Most fields are constants or constant
 *  pointers since they should not change during an operation.
 */
class ClusterOperation
{
private:
    ClusterOperation(const ClusterOperation&) = delete;
    ClusterOperation& operator=(const ClusterOperation&) = delete;

public:
    const OperationType  type;                          // Failover or switchover
    MariaDBServer* const promotion_target;              // Which server will be promoted
    MariaDBServer* const demotion_target;               // Which server will be demoted
    const bool           demotion_target_is_master;     // Was the demotion target the master?
    const bool           handle_events;                 // Should scheduled server events be disabled/enabled?
    const std::string    promotion_sql_file;            // SQL commands ran on a server promoted to master
    const std::string    demotion_sql_file;             // SQL commands ran on a server demoted from master
    const std::string    replication_user;              // User for CHANGE MASTER TO ...
    const std::string    replication_password;          // Password for CHANGE MASTER TO ...
    json_t** const       error_out;                     // Json error output
    maxbase::Duration    time_remaining;                // How much time remains to complete the operation

    /* Slave connections of the demotion target. Saved here in case the data in the server object is
     * modified before promoted server has copied the connections. */
    SlaveStatusArray demotion_target_conns;

    /* Similar copy for promotion target connections. */
    SlaveStatusArray promotion_target_conns;

    ClusterOperation(OperationType type,
                     MariaDBServer* promotion_target, MariaDBServer* demotion_target,
                     const SlaveStatusArray& promo_target_conns, const SlaveStatusArray& demo_target_conns,
                     bool demo_target_is_master, bool handle_events,
                     std::string& promotion_sql_file, std::string& demotion_sql_file,
                     std::string& replication_user, std::string& replication_password,
                     json_t** error, maxbase::Duration time_remaining);
};

/**
 * Helper class for simplifying working with resultsets. Used in MariaDBServer.
 */
class QueryResult
{
    // These need to be banned to avoid premature destruction.
    QueryResult(const QueryResult&) = delete;
    QueryResult& operator=(const QueryResult&) = delete;

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
    int64_t get_current_row_index() const;

    /**
     * How many columns the result set has.
     *
     * @return Column count, or -1 if no data.
     */
    int64_t get_col_count() const;

    /**
     * How many rows does the result set have?
     *
     * @return The number of rows or -1 on error
     */
    int64_t get_row_count() const;

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
    MYSQL_RES*                               m_resultset = NULL;    // Underlying result set, freed at dtor.
    std::unordered_map<std::string, int64_t> m_col_indexes;         // Map of column name -> index
    MYSQL_ROW                                m_rowdata = NULL;      // Data for current row
    int64_t                                  m_current_row_ind = -1;// Index of current row
};
