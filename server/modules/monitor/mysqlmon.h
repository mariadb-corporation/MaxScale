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
    bool warn_failover;            /**< Log a warning when failover happens */
    bool allow_external_slaves;    /**< Whether to allow usage of external slave servers */
    bool failover;                 /**< If master failover is enabled */
    uint32_t failover_timeout;     /**< Timeout in seconds for the master failover */
    bool switchover;               /**< If master switchover is enabled */
    char* switchover_script;       /**< Script to call for performing master switchover */
    uint32_t switchover_timeout;   /**< Timeout in seconds for the master switchover */
    MXS_MONITOR* monitor;
} MYSQL_MONITOR;

MXS_END_DECLS

#endif
