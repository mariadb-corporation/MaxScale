/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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
#include <maxscale/protocol/mariadb/gtid.hh>

class MariaDBServer;

using mariadb::Gtid;
using mariadb::GtidList;

// Helper class for host-port combinations
class EndPoint
{
public:
    EndPoint(const std::string& host, int port);
    EndPoint();

    /**
     * Create endpoint usable for creating a replication connection. Uses private address if defined.
     *
     * @param server Target server
     * @return Endpoint
     */
    static EndPoint replication_endpoint(const SERVER& server);

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

    /**
     * Does the endpoint point to the server?
     */
    bool points_to_server(const SERVER& srv) const;

private:
    mxb::Host m_host;   /* Address and port */
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
    struct Settings
    {
        enum class GtidMode
        {
            NONE,       /* No gtid. Not supported when generating a CHANGE MASTER TO-command */
            CURRENT,    /* Current_Pos */
            SLAVE       /* Slave_Pos */
        };

        Settings(std::string name, EndPoint target, GtidMode gtid_mode, std::string owner);
        Settings(const std::string& name, const SERVER* target, GtidMode gtid_mode);
        explicit Settings(std::string owner);

        /**
         * Create a short description in the form of "Slave connection from <owner> to <[host]:port>."
         *
         * @return Description
         */
        std::string to_string() const;

        std::string name;                       /* Slave connection name. Must be unique for the server. */
        EndPoint    master_endpoint;            /* Master server address & port */
        GtidMode    gtid_mode {GtidMode::NONE}; /* Gtid-mode */
        std::string m_owner;                    /* Name of the owning server. Used for logging. */
    };

    Settings settings;      /* User-defined settings for the slave connection. */

    /* If the master is a monitored server, it's written here. */
    const MariaDBServer* master_server {nullptr};
    /* Has this slave connection been seen connected, meaning that the master server id is correct? */
    bool seen_connected = false;

    int64_t master_server_id = Gtid::SERVER_ID_UNKNOWN; /* The master's server_id value. Valid ids are
                                                         * 32bit unsigned. -1 is unread/error. */
    slave_io_running_t slave_io_running = SLAVE_IO_NO;  /* Slave I/O thread running state: "Yes",
                                                         * "Connecting" or "No" */
    bool        slave_sql_running = false;              /* Slave SQL thread running state, true if "Yes" */
    GtidList    gtid_io_pos;                            /* Gtid I/O position of the slave thread. */
    int64_t     last_io_errno {0};                      /* Last I/O error number */
    std::string last_io_error;                          /* Last IO error encountered. */
    std::string last_sql_error;                         /* Last SQL error encountered. */
    int64_t     received_heartbeats = 0;                /* How many heartbeats the connection has
                                                         * received */

    int64_t seconds_behind_master = mxs::Target::RLAG_UNDEFINED;    /* How much behind the slave is. */

    /* Time of the latest gtid event or heartbeat the slave connection has received, timed by the monitor. */
    maxbase::TimePoint last_data_time = maxbase::Clock::now();

    std::string to_string() const;
    json_t*     to_json() const;

    bool equal(const SlaveStatus& rhs) const;

    static slave_io_running_t slave_io_from_string(const std::string& str);
    static std::string        slave_io_to_string(slave_io_running_t slave_io);
    bool                      should_be_copied(std::string* ignore_reason_out) const;
};

using SlaveStatusArray = std::vector<SlaveStatus>;
using EventNameSet = std::unordered_set<std::string>;

enum class OperationType
{
    SWITCHOVER,         /**< Normal switchover */
    SWITCHOVER_FORCE,   /**< Forced switchover. Ignores several errors. */
    FAILOVER,
    REJOIN,
    UNDO_DEMOTION       /**< Performed when switchover fails in its first stages. */
};

enum class OpStart {MANUAL, AUTO};
enum class SwitchoverType {NORMAL, FORCE};

class GeneralOpData
{
public:
    OpStart           start {OpStart::MANUAL};  // How operation was started
    mxb::Json&        error_out;                // Json error output
    maxbase::Duration time_remaining;           // How much time remains to complete the operation

    GeneralOpData(OpStart start, mxb::Json& error, maxbase::Duration time_remaining);
};

// Operation data which concerns a single server
class ServerOperation
{
public:
    enum TargetType
    {
        MASTER, /**< Swapping master. Either demoting a master or promoting a new master. */
        RELAY   /**< Just swapping a relay with a relay/replica. */
    };

    MariaDBServer* const   target;              // Target server
    const TargetType       target_type {MASTER};// Was the target a master / should it become one
    const SlaveStatusArray conns_to_copy;       // Slave connections the target should copy/merge
    const EventNameSet     events_to_enable;    // Scheduled event names last seen on master.

    ServerOperation(MariaDBServer* target, TargetType target_type, SlaveStatusArray conns_to_copy,
                    EventNameSet events_to_enable);

    ServerOperation(MariaDBServer* target, TargetType target_type);
};

/* Server lock status descriptor */
class ServerLock
{
public:
    enum class Status
    {
        UNKNOWN,        /* Unknown/error */
        FREE,           /* Lock is unclaimed */
        OWNED_SELF,     /* Lock is claimed by current monitor */
        OWNED_OTHER,    /* Lock is claimed by other monitor/MaxScale */
    };

    void    set_status(Status new_status, int64_t owner_id = CONN_ID_UNKNOWN);
    int64_t owner() const;
    Status  status() const;
    bool    is_free() const;

    bool operator==(const ServerLock& rhs) const;
private:
    int64_t m_owner_id {CONN_ID_UNKNOWN};
    Status  m_status {Status::UNKNOWN};
};

int round_to_seconds(mxb::Duration dur);
