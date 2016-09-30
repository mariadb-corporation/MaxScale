#ifndef _MYSQLMON_H
#define _MYSQLMON_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <monitor.h>
#include <spinlock.h>
#include <thread.h>
#include <mysql.h>
#include <mysqld_error.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <secrets.h>
#include <dcb.h>
#include <modinfo.h>
#include <maxconfig.h>
#include <externcmd.h>
#include <hashtable.h>

/**
 * @file mysqlmon.h - The MySQL monitor
 *
 * @verbatim
 * Revision History
 *
 * Date     Who                 Description
 * 08/07/13 Mark Riddoch        Initial implementation
 * 26/05/14 Massimiliano Pinto  Default values for MONITOR_INTERVAL
 * 28/05/14 Massimiliano Pinto  Addition of new fields in MYSQL_MONITOR struct
 * 24/06/14 Massimiliano Pinto  Addition of master field in MYSQL_MONITOR struct and MONITOR_MAX_NUM_SLAVES
 * 28/08/14 Massimiliano Pinto  Addition of detectStaleMaster
 * 30/10/14 Massimiliano Pinto  Addition of disableMasterFailback
 * 07/11/14 Massimiliano Pinto  Addition of NetworkTimeout: connect, read, write
 * 20/04/15 Guillaume Lefranc   Addition of availableWhenDonor
 * 22/04/15 Martin Brampton     Addition of disableMasterRoleSetting
 * 07/05/15 Markus Makela       Addition of command execution on Master server failure
 * @endverbatim
 */

#define MYSQLMON_DEFAULT_FAILCOUNT 5

/**
 * The handle for an instance of a MySQL Monitor module
 */
typedef struct
{
    SPINLOCK lock; /**< The monitor spinlock */
    THREAD thread; /**< Monitor thread */
    int shutdown; /**< Flag to shutdown the monitor thread */
    int status; /**< Monitor status */
    unsigned long id; /**< Monitor ID */
    int replicationHeartbeat; /**< Monitor flag for MySQL replication heartbeat */
    bool detectStaleMaster; /**< Monitor flag for MySQL replication Stale Master detection */
    bool detectStaleSlave; /**< Monitor flag for MySQL replication Stale Master detection */
    bool multimaster; /**< Detect and handle multi-master topologies */
    int disableMasterFailback; /**< Monitor flag for Galera Cluster Master failback */
    int availableWhenDonor; /**< Monitor flag for Galera Cluster Donor availability */
    int disableMasterRoleSetting; /**< Monitor flag to disable setting master role */
    bool mysql51_replication; /**< Use MySQL 5.1 replication */
    MONITOR_SERVERS *master; /**< Master server for MySQL Master/Slave replication */
    char* script; /*< Script to call when state changes occur on servers */
    bool events[MAX_MONITOR_EVENT]; /*< enabled events */
    HASHTABLE *server_info; /**< Contains server specific information */
    bool failover; /**< If simple failover is enabled */
    int failcount; /**< How many monitoring cycles servers must be
                                   down before failover is initiated */
} MYSQL_MONITOR;

#endif
