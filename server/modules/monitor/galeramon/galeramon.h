#ifndef _GALERAMON_H
#define _GALERAMON_H
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
#include <maxscale/monitor.h>
#include <maxscale/spinlock.h>
#include <maxscale/externcmd.h>
#include <maxscale/thread.h>
#include <mysql.h>
#include <mysqld_error.h>
#include <maxscale/skygw_utils.h>
#include <maxscale/log_manager.h>
#include <maxscale/secrets.h>
#include <maxscale/dcb.h>
#include <maxscale/modinfo.h>
#include <maxscale/maxconfig.h>

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
    THREAD thread; /**< Monitor thread */
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
