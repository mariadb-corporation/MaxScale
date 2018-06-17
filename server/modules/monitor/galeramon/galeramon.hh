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

#include <tr1/unordered_map>

#include <maxscale/monitor.hh>

/**
 *  Galera status variables
 */
struct GaleraNode
{
    int         joined;       /**< Node is in sync with the cluster */
    int         local_index;  /**< Node index */
    int         local_state;  /**< Node state */
    int         cluster_size; /**< The cluster size*/
    std::string cluster_uuid; /**< Cluster UUID */
};

typedef std::tr1::unordered_map<MXS_MONITORED_SERVER*, GaleraNode> NodeMap;

class GaleraMonitor : public maxscale::MonitorInstanceSimple
{
public:
    GaleraMonitor(const GaleraMonitor&) = delete;
    GaleraMonitor& operator = (const GaleraMonitor&) = delete;

    ~GaleraMonitor();
    static GaleraMonitor* create(MXS_MONITOR* monitor);
    void diagnostics(DCB* dcb) const;
    json_t* diagnostics_json() const;

protected:
    bool configure(const MXS_CONFIG_PARAMETER* param);
    bool has_sufficient_permissions() const;
    void update_server_status(MXS_MONITORED_SERVER* monitored_server);
    void pre_tick();
    void post_tick();

private:
    unsigned long m_id;                 /**< Monitor ID */
    int m_disableMasterFailback;        /**< Monitor flag for Galera Cluster Master failback */
    int m_availableWhenDonor;           /**< Monitor flag for Galera Cluster Donor availability */
    bool m_disableMasterRoleSetting;    /**< Monitor flag to disable setting master role */
    bool m_root_node_as_master;         /**< Whether we require that the Master should
                                         * have a wsrep_local_index of 0 */
    bool m_use_priority;                /**< Use server priorities */
    bool m_set_donor_nodes;             /**< set the wrep_sst_donor variable with an
                                         * ordered list of nodes */
    std::string m_cluster_uuid;         /**< The Cluster UUID */
    bool m_log_no_members;              /**< Should we log if no member are found. */
    NodeMap m_info;                     /**< Contains Galera Cluster variables of all nodes */
    int m_cluster_size;                 /**< How many nodes in the cluster */

    GaleraMonitor(MXS_MONITOR* monitor);

    bool detect_cluster_size(const int n_nodes,
                             const char *candidate_uuid,
                             const int candidate_size);
    MXS_MONITORED_SERVER *get_candidate_master();
    void set_galera_cluster();
    void update_sst_donor_nodes(int is_cluster);
};
