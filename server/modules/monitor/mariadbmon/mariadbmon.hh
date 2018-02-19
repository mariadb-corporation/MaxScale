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

#include <maxscale/cppdefs.hh>
#include <string>

#include <maxscale/config.h>
#include <maxscale/dcb.h>
#include <maxscale/hashtable.h>
#include <maxscale/monitor.h>
#include <maxscale/thread.h>

using std::string;

// MariaDB Monitor instance data
class MYSQL_MONITOR
{
public:
    MXS_MONITOR* monitor;          /**< Generic monitor object */
    THREAD thread;                 /**< Monitor thread */
    int shutdown;                  /**< Flag to shutdown the monitor thread */
    int status;                    /**< Monitor status */
    MXS_MONITORED_SERVER *master;  /**< Master server for MySQL Master/Slave replication */
    HASHTABLE *server_info;        /**< Contains server specific information */
    bool warn_set_standalone_master; /**< Log a warning when setting standalone master */
    unsigned long id;              /**< Monitor ID */

    // Values updated by monitor
    int64_t master_gtid_domain;    /**< Gtid domain currently used by the master */
    string external_master_host;   /**< External master host, for fail/switchover */
    int external_master_port;      /**< External master port */

    // Replication topology detection settings
    bool mysql51_replication;      /**< Use MySQL 5.1 replication */
    bool detectStaleMaster;        /**< Monitor flag for MySQL replication Stale Master detection */
    bool detectStaleSlave;         /**< Monitor flag for MySQL replication Stale Master detection */
    bool multimaster;              /**< Detect and handle multi-master topologies */
    bool ignore_external_masters;  /**< Ignore masters outside of the monitor configuration */
    bool detect_standalone_master; /**< If standalone master are detected */
    bool replicationHeartbeat;     /**< Monitor flag for MySQL replication heartbeat */

    // Failover, switchover and rejoin settings
    string replication_user;       /**< Replication user for CHANGE MASTER TO-commands */
    string replication_password;   /**< Replication password for CHANGE MASTER TO-commands */
    int failcount;                 /**< How many monitoring cycles master must be down before auto-failover
                                    *   begins */
    uint32_t failover_timeout;     /**< Timeout in seconds for the master failover */
    uint32_t switchover_timeout;   /**< Timeout in seconds for the master switchover */
    bool verify_master_failure;    /**< Whether master failure is verified via slaves */
    int master_failure_timeout;    /**< Master failure verification (via slaves) time in seconds */
    bool auto_failover;            /**< If automatic master failover is enabled */
    bool auto_rejoin;              /**< Attempt to start slave replication on standalone servers or servers
                                    *   replicating from the wrong master automatically. */
    MXS_MONITORED_SERVER** excluded_servers; /**< Servers banned for master promotion during auto-failover. */
    int n_excluded;                /**< Number of excluded servers */

    // Other settings
    string script;                 /**< Script to call when state changes occur on servers */
    uint64_t events;               /**< enabled events */
    bool allow_cluster_recovery;   /**< Allow failed servers to rejoin the cluster */
};
