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
 * @file ndbclustermon.h - The NDB Cluster monitor
 *
 */

/**
 * The handle for an instance of a NDB Cluster Monitor module
 */
typedef struct
{
    SPINLOCK lock; /**< The monitor spinlock */
    pthread_t tid; /**< id of monitor thread */
    int shutdown; /**< Flag to shutdown the monitor thread */
    int status; /**< Monitor status */
    unsigned long id; /**< Monitor ID */
    MONITOR_SERVERS *master; /**< Master server for MySQL Master/Slave replication */
    char* script; /*< Script to call when state changes occur on servers */
    bool events[MAX_MONITOR_EVENT]; /*< enabled events */
} MYSQL_MONITOR;

#endif
