#pragma once
#ifndef _MYSQLMON_H
#define _MYSQLMON_H
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

/**
 * @file mysqlmon.h - The MySQL monitor
 */

#include <maxscale/cdefs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxscale/monitor.h>
#include <maxscale/spinlock.h>
#include <maxscale/thread.h>
#include <mysql.h>
#include <mysqld_error.h>
#include <maxscale/log_manager.h>
#include <maxscale/secrets.h>
#include <maxscale/dcb.h>
#include <maxscale/modinfo.h>
#include <maxscale/config.h>
#include <maxscale/hashtable.h>

MXS_BEGIN_DECLS

/**
 * The handle for an instance of a MySQL Monitor module
 */
typedef struct
{
    THREAD thread;                 /**< Monitor thread */
    int shutdown;                  /**< Flag to shutdown the monitor thread */
    int status;                    /**< Monitor status */
    unsigned long id;              /**< Monitor ID */
    int replicationHeartbeat;      /**< Monitor flag for MySQL replication heartbeat */
    bool detectStaleMaster;        /**< Monitor flag for MySQL replication Stale Master detection */
    bool detectStaleSlave;         /**< Monitor flag for MySQL replication Stale Master detection */
    bool multimaster;              /**< Detect and handle multi-master topologies */
    bool ignore_external_masters;  /**< Ignore masters outside of the monitor configuration */
    int disableMasterFailback;     /**< Monitor flag for Galera Cluster Master failback */
    int availableWhenDonor;        /**< Monitor flag for Galera Cluster Donor availability */
    int disableMasterRoleSetting;  /**< Monitor flag to disable setting master role */
    bool mysql51_replication;      /**< Use MySQL 5.1 replication */
    MXS_MONITORED_SERVER *master;  /**< Master server for MySQL Master/Slave replication */
    char* script;                  /**< Script to call when state changes occur on servers */
    uint64_t events;               /**< enabled events */
    HASHTABLE *server_info;        /**< Contains server specific information */
    bool detect_standalone_master; /**< If standalone master are detected */
    int failcount;                 /**< How many monitoring cycles servers must be
                                      down before failover is initiated */
    bool allow_cluster_recovery;   /**< Allow failed servers to rejoin the cluster */
    bool warn_set_standalone_master; /**< Log a warning when setting standalone master */
    bool auto_failover;            /**< If automatic master failover is enabled */
    uint32_t failover_timeout;     /**< Timeout in seconds for the master failover */
    uint32_t switchover_timeout;   /**< Timeout in seconds for the master switchover */
    char* replication_user;        /**< Replication user for failover */
    char* replication_password;    /**< Replication password for failover*/
    bool verify_master_failure;    /**< Whether master failure is verified via slaves */
    int master_failure_timeout;    /**< Time in seconds to wait before doing failover */
    int64_t master_gtid_domain;    /**< Gtid domain currently used by the master */
    char external_master_host[MAX_SERVER_ADDRESS_LEN]; /**< External master host, for fail/switchover */
    int external_master_port;      /**< External master port */
    bool auto_rejoin;              /**< Attempt to start slave replication on standalone servers or servers
                                        replicating from the wrong master. */
    bool enforce_read_only_slaves; /**< Should the monitor set read-only=1 on any slave servers. */
    int n_excluded;                /**< Number of excluded servers */
    MXS_MONITORED_SERVER** excluded_servers; /**< Servers banned for master promotion during auto-failover. */
    const char* promote_sql_file;  /**< File with sql commands which are ran to a server being promoted. */
    const char* demote_sql_file;   /**< File with sql commands which are ran to a server being demoted. */

    MXS_MONITOR* monitor;
} MYSQL_MONITOR;

MXS_END_DECLS

#endif
