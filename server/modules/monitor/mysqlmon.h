#ifndef _MYSQLMON_H
#define _MYSQLMON_H
/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
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

/**
 * The handle for an instance of a MySQL Monitor module
 */
typedef struct
{
    SPINLOCK lock; /**< The monitor spinlock */
    pthread_t tid; /**< id of monitor thread */
    int shutdown; /**< Flag to shutdown the monitor thread */
    int status; /**< Monitor status */
    unsigned long id; /**< Monitor ID */
    int replicationHeartbeat; /**< Monitor flag for MySQL replication heartbeat */
    int detectStaleMaster; /**< Monitor flag for MySQL replication Stale Master detection */
    int disableMasterFailback; /**< Monitor flag for Galera Cluster Master failback */
    int availableWhenDonor; /**< Monitor flag for Galera Cluster Donor availability */
    int disableMasterRoleSetting; /**< Monitor flag to disable setting master role */
    bool mysql51_replication; /**< Use MySQL 5.1 replication */
    MONITOR_SERVERS *master; /**< Master server for MySQL Master/Slave replication */
    char* script; /*< Script to call when state changes occur on servers */
    bool events[MAX_MONITOR_EVENT]; /*< enabled events */
} MYSQL_MONITOR;

#endif
