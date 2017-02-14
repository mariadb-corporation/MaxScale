#pragma once
#ifndef _MMMON_H
#define _MMMON_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
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

/**
 * @file mmmon.h - The Multi-Master monitor
 */

MXS_BEGIN_DECLS

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
    MXS_MONITOR_SERVERS *master; /**< Master server for Master/Slave replication */
    char* script; /*< Script to call when state changes occur on servers */
    uint64_t events; /*< enabled events */
} MM_MONITOR;

MXS_END_DECLS

#endif
