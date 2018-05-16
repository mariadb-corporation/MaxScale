#pragma once
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
 * @file galeramon.hh - The Galera cluster monitor
 */

#include <maxscale/cppdefs.hh>
#include <maxscale/hashtable.h>
#include <maxscale/monitor.hh>
#include <maxscale/thread.h>

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


class GaleraMonitor : public maxscale::MonitorInstance
{
public:
    GaleraMonitor(const GaleraMonitor&) = delete;
    GaleraMonitor& operator = (const GaleraMonitor&) = delete;

    static GaleraMonitor* create(MXS_MONITOR* monitor);
    void destroy();
    void diagnostics(DCB* dcb) const;
    json_t* diagnostics_json() const;

private:
    unsigned long m_id;                 /**< Monitor ID */
    int m_disableMasterFailback;        /**< Monitor flag for Galera Cluster Master failback */
    int m_availableWhenDonor;           /**< Monitor flag for Galera Cluster Donor availability */
    bool m_disableMasterRoleSetting;    /**< Monitor flag to disable setting master role */
    MXS_MONITORED_SERVER *m_master;     /**< Master server for MySQL Master/Slave replication */
    bool m_root_node_as_master;         /**< Whether we require that the Master should
                                         * have a wsrep_local_index of 0 */
    bool m_use_priority;                /**< Use server priorities */
    bool m_set_donor_nodes;             /**< set the wrep_sst_donor variable with an
                                         * ordered list of nodes */
    HASHTABLE *m_galera_nodes_info;     /**< Contains Galera Cluster variables of all nodes */
    GALERA_CLUSTER_INFO m_cluster_info; /**< Contains Galera cluster info */
    bool m_log_no_members;              /**< Should we log if no member are found. */

    GaleraMonitor(MXS_MONITOR* monitor);
    ~GaleraMonitor();

    bool detect_cluster_size(const int n_nodes,
                             const char *candidate_uuid,
                             const int candidate_size);
    MXS_MONITORED_SERVER *get_candidate_master();
    void monitorDatabase(MXS_MONITORED_SERVER *database);
    void reset_cluster_info();
    void set_cluster_members();
    void set_galera_cluster();
    void update_sst_donor_nodes(int is_cluster);

    bool has_sufficient_permissions() const;
    void configure(const MXS_CONFIG_PARAMETER* param);
    void tick();

    void main();
};
