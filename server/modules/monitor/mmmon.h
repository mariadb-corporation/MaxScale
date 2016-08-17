#ifndef _MMMON_H
#define _MMMON_H
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

/**
 * @file mmmon.h - The Multi-Master monitor
 */

/**
 * The handle for an instance of a Multi-Master Monitor module
 */
typedef struct
{
    SPINLOCK lock; /**< The monitor spinlock */
    THREAD thread; /**< Monitor thread */
    int shutdown; /**< Flag to shutdown the monitor thread */
    int status; /**< Monitor status */
    unsigned long id; /**< Monitor ID */
    int detectStaleMaster; /**< Monitor flag for Stale Master detection */
    MONITOR_SERVERS *master; /**< Master server for Master/Slave replication */
    char* script; /*< Script to call when state changes occur on servers */
    bool events[MAX_MONITOR_EVENT]; /*< enabled events */
} MM_MONITOR;

#endif
