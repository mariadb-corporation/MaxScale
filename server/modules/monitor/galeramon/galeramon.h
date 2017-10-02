#pragma once
#ifndef _GALERAMON_H
#define _GALERAMON_H
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
#include <maxscale/hashtable.h>

MXS_BEGIN_DECLS

/**
 *  Galera Variables and server reference for each
 *  monitored node that could be part of cluster.
 *
 *  This struct is added to the HASHTABLE *galera_nodes_info
 */
typedef struct galera_node_info
{
    int          joined; /**< The node claims to be "Synced" */
    int     local_index; /**< wsrep_local_index Galera variable:
                           * the node index vlaue in the cluster */
    int     local_state; /**< wsrep_local_state Galera variable:
                           * the node state in the cluster */
    int    cluster_size; /**< wsrep_cluster_size Galera variable:
                           * the cluster size the node sees */
    char  *cluster_uuid; /**< wsrep_cluster_uuid Galera variable:
                           * the cluster UUID the node sees */
    const SERVER  *node; /**< The reference to nodes' SERVER struct */
} GALERA_NODE_INFO;

/**
 * Information of the current detected
 * Galera Cluster
 */
typedef struct galera_cluster_info
{
    int   c_size; /**< How many nodes in the cluster */
    char *c_uuid; /**< The Cluster UUID */
} GALERA_CLUSTER_INFO;

/**
 * The handle for an instance of a Galera Monitor module
 */
typedef struct
{
    THREAD thread; /**< Monitor thread */
    int shutdown; /**< Flag to shutdown the monitor thread */
    int status; /**< Monitor status */
    unsigned long id; /**< Monitor ID */
    int disableMasterFailback; /**< Monitor flag for Galera Cluster Master failback */
    int availableWhenDonor; /**< Monitor flag for Galera Cluster Donor availability */
    bool disableMasterRoleSetting; /**< Monitor flag to disable setting master role */
    MXS_MONITORED_SERVER *master; /**< Master server for MySQL Master/Slave replication */
    char* script;
    bool root_node_as_master; /**< Whether we require that the Master should
                                    * have a wsrep_local_index of 0 */
    bool use_priority; /*< Use server priorities */
    uint64_t events; /*< enabled events */
    bool set_donor_nodes; /**< set the wrep_sst_donor variable with an
                           * ordered list of nodes */
    HASHTABLE *galera_nodes_info; /**< Contains Galera Cluster variables of all nodes */
    GALERA_CLUSTER_INFO cluster_info; /**< Contains Galera cluster info */
    MXS_MONITOR* monitor;
} GALERA_MONITOR;

MXS_END_DECLS

#endif
