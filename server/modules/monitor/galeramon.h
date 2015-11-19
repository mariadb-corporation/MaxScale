#ifndef _GALERAMON_H
#define _GALERAMON_H
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
#include <externcmd.h>
#include <thread.h>
#include <mysql.h>
#include <mysqld_error.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <secrets.h>
#include <dcb.h>
#include <modinfo.h>
#include <maxconfig.h>

/**
 * @file galeramon.h - The Galera cluster monitor
 *
 * @verbatim
 * Revision History
 *
 * Date      Who             Description
 * 07/05/15  Markus Makela   Initial Implementation of galeramon.h
 * @endverbatim
 */

/**
 * The handle for an instance of a Galera Monitor module
 */
typedef struct
{
    SPINLOCK lock; /**< The monitor spinlock */
    pthread_t tid; /**< id of monitor thread */
    int shutdown; /**< Flag to shutdown the monitor thread */
    int status; /**< Monitor status */
    unsigned long id; /**< Monitor ID */
    int disableMasterFailback; /**< Monitor flag for Galera Cluster Master failback */
    int availableWhenDonor; /**< Monitor flag for Galera Cluster Donor availability */
    int disableMasterRoleSetting; /**< Monitor flag to disable setting master role */
    MONITOR_SERVERS *master; /**< Master server for MySQL Master/Slave replication */
    char* script;
    bool use_priority; /*< Use server priorities */
    bool events[MAX_MONITOR_EVENT]; /*< enabled events */
} GALERA_MONITOR;

#endif
