#ifndef _MMMON_H
#define _MMMON_H
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
 * Copyright MariaDB Corporation Ab 2015
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
 * @file mmmon.h - The Multi-Master monitor
 */

/**
 * The handle for an instance of a Multi-Master Monitor module
 */
typedef struct
{
    SPINLOCK lock; /**< The monitor spinlock */
    pthread_t tid; /**< id of monitor thread */
    int shutdown; /**< Flag to shutdown the monitor thread */
    int status; /**< Monitor status */
    unsigned long id; /**< Monitor ID */
    int detectStaleMaster; /**< Monitor flag for Stale Master detection */
    MONITOR_SERVERS *master; /**< Master server for Master/Slave replication */
    char* script; /*< Script to call when state changes occur on servers */
    bool events[MAX_MONITOR_EVENT]; /*< enabled events */
} MM_MONITOR;

#endif
