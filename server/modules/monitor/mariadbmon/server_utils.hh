/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once
#include "mariadbmon_common.hh"

#include <unordered_set>
#include <string>
#include <vector>
#include <maxbase/host.hh>
#include <maxbase/stopwatch.hh>
#include <maxscale/server.hh>

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
    using DomainList = std::vector<uint32_t>;

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

    /**
     * Return all of the domains in this GtidList.
     *
     * @return Array of domains
     */
    DomainList domains() const;

private:
    std::vector<Gtid> m_triplets;
};

// Helper class for host-port combinations
class EndPoint
{
public:
    EndPoint(const std::string& host, int port);
    explicit EndPoint(const SERVER* server);
    EndPoint();

    std::string host() const
    {
        return m_host.address();
    }

    int port() const
    {
        return m_host.port();
    }

    bool operator==(const EndPoint& rhs) const;
    bool operator!=(const EndPoint& rhs) const
    {
        return !(*this == rhs);
    }

    std::string to_string() const;

private:
    mxb::Host   m_host;  /* Address and port */
};


// Contains data returned by one row of SHOW ALL SLAVES STATUS
class SlaveStatus
{
public:
    explicit SlaveStatus(const std::string& owner);

    enum slave_io_running_t
    {
        SLAVE_IO_YES,
        SLAVE_IO_CONNECTING,
        SLAVE_IO_NO,
    };

    // Helper class for containing slave connection settings. These are modifiable by
    // a CHANGE MASTER TO-command and should not change on their own. The owning server
    // is included to simplify log message creation.
    class Settings
    {
    public:
        Settings(const std::string& name, const EndPoint& target, const std::string& owner);
        Settings(const std::string& name, const SERVER* target);
        explicit Settings(const std::string& owner);

        /**
         * Create a short description in the form of "Slave connection from <owner> to <[host]:port>."
         *
         * @return Description
         */
        std::string to_string() const;

        std::string name;             /* Slave connection name. Must be unique for the server. */
        EndPoint    master_endpoint;  /* Master server address & port */

    private:
        std::string m_owner;          /* Name of the owning server. Used for logging. */
    };

    Settings settings;  /* User-defined settings for the slave connection. */

    /* If the master is a monitored server, it's written here. */
    const MariaDBServer* master_server {nullptr};
    /* Has this slave connection been seen connected, meaning that the master server id is correct? */
    bool seen_connected = false;

    int64_t master_server_id = SERVER_ID_UNKNOWN;       /* The master's server_id value. Valid ids are
                                                         * 32bit unsigned. -1 is unread/error. */
    slave_io_running_t slave_io_running = SLAVE_IO_NO;  /* Slave I/O thread running state: "Yes",
                                                         * "Connecting" or "No" */
    bool        slave_sql_running = false;              /* Slave SQL thread running state, true if "Yes" */
    GtidList    gtid_io_pos;                            /* Gtid I/O position of the slave thread. */
    std::string last_error;                             /* Last IO or SQL error encountered. */
    int64_t     received_heartbeats = 0;                /* How many heartbeats the connection has
                                                         * received */

    int seconds_behind_master = SERVER::RLAG_UNDEFINED;     /* How much behind the slave is. */

    /* Time of the latest gtid event or heartbeat the slave connection has received, timed by the monitor. */
    maxbase::Clock::time_point last_data_time = maxbase::Clock::now();

    std::string to_string() const;
    json_t*     to_json() const;

    static slave_io_running_t slave_io_from_string(const std::string& str);
    static std::string        slave_io_to_string(slave_io_running_t slave_io);
    bool                      should_be_copied(std::string* ignore_reason_out) const;
};

using SlaveStatusArray = std::vector<SlaveStatus>;
using EventNameSet = std::unordered_set<std::string>;

enum class OperationType
{
    SWITCHOVER,
    FAILOVER,
    REJOIN,
    UNDO_DEMOTION // Performed when switchover fails in its first stages.
};

class GeneralOpData
{
public:
    json_t** const    error_out;                    // Json error output
    maxbase::Duration time_remaining;               // How much time remains to complete the operation

    GeneralOpData(json_t** error, maxbase::Duration time_remaining);
};

// Operation data which concerns a single server
class ServerOperation
{
public:
    MariaDBServer* const   target;          // Target server
    const bool             to_from_master;  // Was the target a master / should it become one
    const SlaveStatusArray conns_to_copy;   // Slave connections the target should copy/merge

    const EventNameSet events_to_enable;    // Scheduled event names last seen on master.

    ServerOperation(MariaDBServer* target, bool was_is_master,
                    const SlaveStatusArray& conns_to_copy,
                    const EventNameSet& events_to_enable);

    ServerOperation(MariaDBServer* target, bool was_is_master);
};

// Settings shared between the MariaDB-Monitor and the MariaDB-Servers.
// These are only written to when configuring the monitor.
class SharedSettings
{
public:
    // Required by cluster operations
    std::string replication_user;         /**< Username for CHANGE MASTER TO-commands */
    std::string replication_password;     /**< Password for CHANGE MASTER TO-commands */
    bool        replication_ssl {false};  /**< Set MASTER_SSL = 1 in CHANGE MASTER TO-commands */

    std::string promotion_sql_file;  /**< File with sql commands which are ran to a server being
                                       *  promoted. */
    std::string demotion_sql_file;   /**< File with sql commands which are ran to a server being
                                       *  demoted. */

    /* Should failover/switchover enable/disable any scheduled events on the servers during
     * promotion/demotion? */
    bool handle_event_scheduler {true};
};
