#pragma once
#ifndef _GALERAMON_H
#define _GALERAMON_H
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

MXS_BEGIN_DECLS

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
    bool disableMasterRoleSetting; /**< Monitor flag to disable setting master role */
    MXS_MONITOR_SERVERS *master; /**< Master server for MySQL Master/Slave replication */
    char* script;
    bool root_node_as_master; /**< Whether we require that the Master should
                                    * have a wsrep_local_index of 0 */
    bool use_priority; /*< Use server priorities */
    uint64_t events; /*< enabled events */
    bool set_donor_nodes; /**< set the wrep_sst_donor variable with an
                           * ordered list of nodes */
} GALERA_MONITOR;

MXS_END_DECLS

#endif
