#pragma once
#ifndef _NDBCMON_H
#define _NDBCMON_H
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
 * @file ndbclustermon.h - The NDB Cluster monitor
 *
 */

#include <maxscale/monitor.h>
#include <maxscale/spinlock.h>
#include <maxscale/thread.h>

// The handle for an instance of a NDB Cluster Monitor module
typedef struct
{
    THREAD thread; /**< Monitor thread */
    SPINLOCK lock; /**< The monitor spinlock */
    unsigned long id; /**< Monitor ID */
    uint64_t events; /*< enabled events */
    int shutdown; /**< Flag to shutdown the monitor thread */
    int status; /**< Monitor status */
    MXS_MONITORED_SERVER *master; /**< Master server for MySQL Master/Slave replication */
    char* script; /*< Script to call when state changes occur on servers */
    MXS_MONITOR* monitor;
} NDBC_MONITOR;

#endif
