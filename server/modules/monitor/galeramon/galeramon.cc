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
 * @file galera_mon.c - A MySQL Galera cluster monitor
 */

#define MXS_MODULE_NAME "galeramon"

#include "galeramon.hh"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql.h>
#include <mysqld_error.h>
#include <maxscale/alloc.h>
#include <maxscale/config.h>
#include <maxscale/dcb.h>
#include <maxscale/log_manager.h>
#include <maxscale/modinfo.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/secrets.h>
#include <maxscale/spinlock.h>

#define DONOR_NODE_NAME_MAX_LEN 60
#define DONOR_LIST_SET_VAR "SET GLOBAL wsrep_sst_donor = \""

/** Log a warning when a bad 'wsrep_local_index' is found */
static bool warn_erange_on_local_index = true;

static MXS_MONITORED_SERVER *set_cluster_master(MXS_MONITORED_SERVER *, MXS_MONITORED_SERVER *, int);
static void disableMasterFailback(void *, int);
static int compare_node_index(const void*, const void*);
static int compare_node_priority(const void*, const void*);
static GALERA_NODE_INFO *nodeval_dup(const GALERA_NODE_INFO *);
static void nodeval_free(GALERA_NODE_INFO *);
static bool using_xtrabackup(MXS_MONITORED_SERVER *database, const char* server_string);

GaleraMonitor::GaleraMonitor(MXS_MONITOR *mon)
    : maxscale::MonitorInstance(mon)
    , m_id(MXS_MONITOR_DEFAULT_ID)
    , m_disableMasterFailback(0)
    , m_availableWhenDonor(0)
    , m_disableMasterRoleSetting(0)
    , m_root_node_as_master(false)
    , m_use_priority(false)
    , m_set_donor_nodes(false)
    , m_galera_nodes_info(NULL)
    , m_log_no_members(false)
{
    HASHTABLE *nodes_info = hashtable_alloc(MAX_NUM_SLAVES,
                                            hashtable_item_strhash,
                                            hashtable_item_strcmp);
    ss_dassert(nodes_info);
    if (!nodes_info)
    {
        throw std::bad_alloc();
    }

    hashtable_memory_fns(nodes_info,
                         hashtable_item_strdup,
                         (HASHCOPYFN)nodeval_dup,
                         hashtable_item_free,
                         (HASHFREEFN)nodeval_free);

    m_galera_nodes_info = nodes_info;
    m_cluster_info.c_size = 0;
    m_cluster_info.c_uuid = NULL;
}

GaleraMonitor::~GaleraMonitor()
{
    hashtable_free(m_galera_nodes_info);
}

// static
GaleraMonitor* GaleraMonitor::create(MXS_MONITOR* monitor)
{
    return new GaleraMonitor(monitor);
}

void GaleraMonitor::diagnostics(DCB *dcb) const
{
    dcb_printf(dcb, "Master Failback:\t%s\n", (m_disableMasterFailback == 1) ? "off" : "on");
    dcb_printf(dcb, "Available when Donor:\t%s\n", (m_availableWhenDonor == 1) ? "on" : "off");
    dcb_printf(dcb, "Master Role Setting Disabled:\t%s\n",
               m_disableMasterRoleSetting ? "on" : "off");
    dcb_printf(dcb, "Set wsrep_sst_donor node list:\t%s\n", (m_set_donor_nodes == 1) ? "on" : "off");
    if (m_cluster_info.c_uuid)
    {
        dcb_printf(dcb, "Galera Cluster UUID:\t%s\n", m_cluster_info.c_uuid);
        dcb_printf(dcb, "Galera Cluster size:\t%d\n", m_cluster_info.c_size);
    }
    else
    {
        dcb_printf(dcb, "Galera Cluster NOT set:\tno member nodes\n");
    }
}

json_t* GaleraMonitor::diagnostics_json() const
{
    json_t* rval = MonitorInstance::diagnostics_json();
    json_object_set_new(rval, "disable_master_failback", json_boolean(m_disableMasterFailback));
    json_object_set_new(rval, "disable_master_role_setting", json_boolean(m_disableMasterRoleSetting));
    json_object_set_new(rval, "root_node_as_master", json_boolean(m_root_node_as_master));
    json_object_set_new(rval, "use_priority", json_boolean(m_use_priority));
    json_object_set_new(rval, "set_donor_nodes", json_boolean(m_set_donor_nodes));

    if (m_cluster_info.c_uuid)
    {
        json_object_set_new(rval, "cluster_uuid", json_string(m_cluster_info.c_uuid));
        json_object_set_new(rval, "cluster_size", json_integer(m_cluster_info.c_size));
    }

    return rval;
}

bool GaleraMonitor::configure(const MXS_CONFIG_PARAMETER* params)
{
    m_disableMasterFailback = config_get_bool(params, "disable_master_failback");
    m_availableWhenDonor = config_get_bool(params, "available_when_donor");
    m_disableMasterRoleSetting = config_get_bool(params, "disable_master_role_setting");
    m_root_node_as_master = config_get_bool(params, "root_node_as_master");
    m_use_priority = config_get_bool(params, "use_priority");
    m_set_donor_nodes = config_get_bool(params, "set_donor_nodes");
    m_log_no_members = true;

    /* Reset all data in the hashtable */
    reset_cluster_info();

    return true;
}

bool GaleraMonitor::has_sufficient_permissions() const
{
    return check_monitor_permissions(m_monitor, "SHOW STATUS LIKE 'wsrep_local_state'");
}

void GaleraMonitor::update_server_status(MXS_MONITORED_SERVER* monitored_server)
{
    MYSQL_ROW row;
    MYSQL_RES *result;
    char *server_string;

    /* get server version string */
    mxs_mysql_set_server_version(monitored_server->con, monitored_server->server);
    server_string = monitored_server->server->version_string;

    /* Check if the the Galera FSM shows this node is joined to the cluster */
    const char *cluster_member =
        "SHOW STATUS WHERE Variable_name IN"
        " ('wsrep_cluster_state_uuid',"
        " 'wsrep_cluster_size',"
        " 'wsrep_local_index',"
        " 'wsrep_local_state')";

    if (mxs_mysql_query(monitored_server->con, cluster_member) == 0
        && (result = mysql_store_result(monitored_server->con)) != NULL)
    {
        if (mysql_field_count(monitored_server->con) < 2)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for \"%s\". "
                      "Expected 2 columns. MySQL Version: %s",
                      cluster_member, server_string);
            return;
        }
        GALERA_NODE_INFO info = {};
        while ((row = mysql_fetch_row(result)))
        {
            if (strcmp(row[0], "wsrep_cluster_size") == 0)
            {
                info.cluster_size = atoi(row[1]);
            }

            if (strcmp(row[0], "wsrep_local_index") == 0)
            {
                char* endchar;
                long local_index = strtol(row[1], &endchar, 10);
                if (*endchar != '\0' ||
                    (errno == ERANGE && (local_index == LONG_MAX || local_index == LONG_MIN)))
                {
                    if (warn_erange_on_local_index)
                    {
                        MXS_WARNING("Invalid 'wsrep_local_index' on server '%s': %s",
                                    monitored_server->server->name, row[1]);
                        warn_erange_on_local_index = false;
                    }
                    local_index = -1;
                    /* Force joined = 0 */
                    info.joined = 0;
                }

                info.local_index = local_index;
            }

            ss_dassert(row[0] && row[1]);

            if (strcmp(row[0], "wsrep_local_state") == 0)
            {
                if (strcmp(row[1], "4") == 0)
                {
                    info.joined = 1;
                }
                /* Check if the node is a donor and is using xtrabackup, in this case it can stay alive */
                else if (strcmp(row[1], "2") == 0 && m_availableWhenDonor == 1 &&
                         using_xtrabackup(monitored_server, server_string))
                {
                    info.joined = 1;
                }
                else
                {
                    /* Force joined = 0 */
                    info.joined = 0;
                }

                info.local_state = atoi(row[1]);
            }

            /* We can check:
             * wsrep_local_state == 0
             * wsrep_cluster_size == 0
             * wsrep_cluster_state_uuid == ""
             */
            if (strcmp(row[0], "wsrep_cluster_state_uuid") == 0)
            {
                if (row[1] == NULL || !strlen(row[1]))
                {
                    MXS_DEBUG("Node %s is not running Galera Cluster",
                              monitored_server->server->name);
                    info.cluster_uuid = NULL;
                    info.joined = 0;
                }
                else
                {
                    info.cluster_uuid = MXS_STRDUP(row[1]);
                }
            }
        }

        monitored_server->server->node_id = info.joined ? info.local_index : -1;

        /* Add server pointer */
        info.node = monitored_server->server;

        /* Galera Cluster vars fetch */
        HASHTABLE *table = m_galera_nodes_info;
        GALERA_NODE_INFO *node =
            static_cast<GALERA_NODE_INFO*>(hashtable_fetch(table, monitored_server->server->name));
        if (node)
        {
            MXS_DEBUG("Node %s is present in galera_nodes_info, updtating info",
                      monitored_server->server->name);

            MXS_FREE(node->cluster_uuid);
            /* Update node data */
            memcpy(node, &info, sizeof(GALERA_NODE_INFO));
        }
        else
        {
            if (hashtable_add(table, monitored_server->server->name, &info))
            {
                MXS_DEBUG("Added %s to galera_nodes_info",
                          monitored_server->server->name);
            }
            /* Free the info.cluster_uuid as it's been added to the table */
            MXS_FREE(info.cluster_uuid);
        }

        MXS_DEBUG("Server %s: local_state %d, local_index %d, UUID %s, size %d, possible member %d",
                  monitored_server->server->name,
                  info.local_state,
                  info.local_index,
                  info.cluster_uuid ? info.cluster_uuid : "_none_",
                  info.cluster_size,
                  info.joined);

        mysql_free_result(result);
    }
    else
    {
        mon_report_query_error(monitored_server);
    }
}

void GaleraMonitor::tick()
{
    MonitorInstance::tick();

    int is_cluster = 0;

    /* Try to set a Galera cluster based on
     * UUID and cluster_size each node reports:
     * no multiple clusters UUID are allowed.
     */
    set_galera_cluster();

    /*
     * Let's select a master server:
     * it could be the candidate master following MXS_MIN(node_id) rule or
     * the server that was master in the previous monitor polling cycle
     * Decision depends on master_stickiness value set in configuration
     */

    /* get the candidate master, following MXS_MIN(node_id) rule */
    MXS_MONITORED_SERVER *candidate_master = get_candidate_master();

    m_master = set_cluster_master(m_master, candidate_master, m_disableMasterFailback);

    MXS_MONITORED_SERVER *ptr = m_monitor->monitored_servers;

    while (ptr)
    {
        const int repl_bits = (SERVER_SLAVE | SERVER_MASTER | SERVER_MASTER_STICKINESS);
        if (SERVER_IS_JOINED(ptr->server) && !m_disableMasterRoleSetting)
        {
            if (ptr != m_master)
            {
                /* set the Slave role and clear master stickiness */
                monitor_clear_pending_status(ptr, repl_bits);
                monitor_set_pending_status(ptr, SERVER_SLAVE);
            }
            else
            {
                if (candidate_master &&
                    m_master->server->node_id != candidate_master->server->node_id)
                {
                    /* set master role and master stickiness */
                    monitor_clear_pending_status(ptr, repl_bits);
                    monitor_set_pending_status(ptr, SERVER_MASTER | SERVER_MASTER_STICKINESS);
                }
                else
                {
                    /* set master role and clear master stickiness */
                    monitor_clear_pending_status(ptr, repl_bits);
                    monitor_set_pending_status(ptr, SERVER_MASTER);
                }
            }

            is_cluster++;
        }
        else
        {
            monitor_clear_pending_status(ptr, repl_bits);
            monitor_set_pending_status(ptr, 0);
        }
        ptr = ptr->next;
    }

    if (is_cluster == 0 && m_log_no_members)
    {
        MXS_ERROR("There are no cluster members");
        m_log_no_members = false;
    }
    else
    {
        if (is_cluster > 0 && m_log_no_members == 0)
        {
            MXS_NOTICE("Found cluster members");
            m_log_no_members = true;
        }
    }

    /* Set the global var "wsrep_sst_donor"
     * with a sorted list of "wsrep_node_name" for slave nodes
     */
    if (m_set_donor_nodes)
    {
        update_sst_donor_nodes(is_cluster);
    }
}

static bool using_xtrabackup(MXS_MONITORED_SERVER *database, const char* server_string)
{
    bool rval = false;
    MYSQL_RES* result;

    if (mxs_mysql_query(database->con, "SHOW VARIABLES LIKE 'wsrep_sst_method'") == 0
        && (result = mysql_store_result(database->con)) != NULL)
    {
        if (mysql_field_count(database->con) < 2)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for \"SHOW VARIABLES LIKE "
                      "'wsrep_sst_method'\". Expected 2 columns."
                      " MySQL Version: %s", server_string);
        }

        MYSQL_ROW row;

        while ((row = mysql_fetch_row(result)))
        {
            if (row[1] && strncmp(row[1], "xtrabackup", 10) == 0)
            {
                rval = true;
            }
        }
        mysql_free_result(result);
    }
    else
    {
        mon_report_query_error(database);
    }

    return rval;
}

/**
 * get candidate master from all nodes
 *
 * The current available rule: get the server with MXS_MIN(node_id)
 * node_id comes from 'wsrep_local_index' variable
 *
 * @param   servers The monitored servers list
 * @return  The candidate master on success, NULL on failure
 */
MXS_MONITORED_SERVER *GaleraMonitor::get_candidate_master()
{
    MXS_MONITORED_SERVER *moitor_servers = m_monitor->monitored_servers;
    MXS_MONITORED_SERVER *candidate_master = NULL;
    long min_id = -1;
    int minval = INT_MAX;
    int currval;
    /* set min_id to the lowest value of moitor_servers->server->node_id */
    while (moitor_servers)
    {
        if (!SERVER_IN_MAINT(moitor_servers->server) && SERVER_IS_JOINED(moitor_servers->server))
        {

            moitor_servers->server->depth = 0;
            char buf[50]; // Enough to hold most numbers
            if (m_use_priority && server_get_parameter_nolock(moitor_servers->server, "priority", buf, sizeof(buf)))
            {
                /** The server has a priority  */
                if ((currval = atoi(buf)) > 0)
                {
                    /** The priority is valid */
                    if (currval < minval && currval > 0)
                    {
                        minval = currval;
                        candidate_master = moitor_servers;
                    }
                }
            }
            else if (moitor_servers->server->node_id >= 0 &&
                     (!m_use_priority || candidate_master == NULL))
            {
                // Server priorities are not in use or no candidate has been found
                if (min_id < 0 || moitor_servers->server->node_id < min_id)
                {
                    min_id = moitor_servers->server->node_id;
                    candidate_master = moitor_servers;
                }
            }
        }
        moitor_servers = moitor_servers->next;
    }

    if (!m_use_priority && !m_disableMasterFailback  &&
        m_root_node_as_master && min_id > 0)
    {
        /** The monitor couldn't find the node with wsrep_local_index of 0.
         * This means that we can't connect to the root node of the cluster.
         *
         * If the node is down, the cluster would recalculate the index values
         * and we would find it. In this case, we just can't connect to it.
         */

        candidate_master = NULL;
    }

    return candidate_master;
}

/**
 * set the master server in the cluster
 *
 * master could be the last one from previous monitor cycle Iis running) or
 * the candidate master.
 * The selection is based on the configuration option mapped to master_stickiness
 * The candidate master may change over time due to
 * 'wsrep_local_index' value change in the Galera Cluster
 * Enabling master_stickiness will avoid master change unless a failure is spotted
 *
 * @param   current_master Previous master server
 * @param   candidate_master The candidate master server accordingly to the selection rule
 * @return  The  master node pointer (could be NULL)
 */
static MXS_MONITORED_SERVER *set_cluster_master(MXS_MONITORED_SERVER *current_master,
                                               MXS_MONITORED_SERVER *candidate_master,
                                               int master_stickiness)
{
    /*
     * if current master is not set or master_stickiness is not enable
     * just return candidate_master.
     */
    if (current_master == NULL || master_stickiness == 0)
    {
        return candidate_master;
    }
    else
    {
        /*
         * if current_master is still a cluster member use it
         *
         */
        if (SERVER_IS_JOINED(current_master->server) && (!SERVER_IN_MAINT(current_master->server)))
        {
            return current_master;
        }
        else
        {
            return candidate_master;
        }
    }
}

/**
 * Set the global variable wsrep_sst_donor in the cluster
 *
 * The monitor user must have the privileges for setting global vars.
 *
 * Galera monitor fetches from each joined slave node the var 'wsrep_node_name'
 * A list of nodes is automatically build and it's sorted by wsrep_local_index DESC
 * or by priority ASC if use_priority option is set.
 *
 * The list is then added to SET GLOBAL VARIABLE wrep_sst_donor =
 * The variable must be sent to all slave nodes.
 *
 * All slave nodes have a sorted list of nodes tht can be used as donor nodes.
 *
 * If there is only one node the funcion returns,
 *
 * @param   mon        The monitor handler
 * @param   is_cluster The number of joined nodes
 */
void GaleraMonitor::update_sst_donor_nodes(int is_cluster)
{
    MXS_MONITORED_SERVER *ptr;
    MYSQL_ROW row;
    MYSQL_RES *result;
    bool ignore_priority = true;

    if (is_cluster == 1)
    {
        MXS_DEBUG("Only one server in the cluster: update_sst_donor_nodes is not performed");
        return;
    }

    unsigned int found_slaves = 0;
    MXS_MONITORED_SERVER *node_list[is_cluster - 1];
    /* Donor list size = DONOR_LIST_SET_VAR + n_hosts * max_host_len + n_hosts + 1 */

    char *donor_list = static_cast<char*>(MXS_CALLOC(1, strlen(DONOR_LIST_SET_VAR) +
                                                     is_cluster * DONOR_NODE_NAME_MAX_LEN +
                                                     is_cluster + 1));

    if (donor_list == NULL)
    {
        MXS_ERROR("can't execute update_sst_donor_nodes() due to memory allocation error");
        return;
    }

    strcpy(donor_list, DONOR_LIST_SET_VAR);

    ptr = m_monitor->monitored_servers;

    /* Create an array of slave nodes */
    while (ptr)
    {
        if (SERVER_IS_JOINED(ptr->server) && SERVER_IS_SLAVE(ptr->server))
        {
            node_list[found_slaves] = (MXS_MONITORED_SERVER *)ptr;
            found_slaves++;

            /* Check the server parameter "priority"
             * If no server has "priority" set, then
             * the server list will be order by default method.
             */

            if (m_use_priority &&
                server_get_parameter_nolock(ptr->server, "priority", NULL, 0))
            {
                ignore_priority = false;
            }
        }
        ptr = ptr->next;
    }

    if (ignore_priority && m_use_priority)
    {
        MXS_DEBUG("Use priority is set but no server has priority parameter. "
                  "Donor server list will be ordered by 'wsrep_local_index'");
    }

    /* Set order type */
    bool sort_order = (!ignore_priority) && (int)m_use_priority;

    /* Sort the array */
    qsort(node_list,
          found_slaves,
          sizeof(MXS_MONITORED_SERVER *),
          sort_order ? compare_node_priority : compare_node_index);

    /* Select nodename from each server and append it to node_list */
    for (unsigned int k = 0; k < found_slaves; k++)
    {
        MXS_MONITORED_SERVER *ptr = node_list[k];

        /* Get the Galera node name */
        if (mxs_mysql_query(ptr->con, "SHOW VARIABLES LIKE 'wsrep_node_name'") == 0
            && (result = mysql_store_result(ptr->con)) != NULL)
        {
            if (mysql_field_count(ptr->con) < 2)
            {
                mysql_free_result(result);
                MXS_ERROR("Unexpected result for \"SHOW VARIABLES LIKE 'wsrep_node_name'\". "
                          "Expected 2 columns");
                return;
            }

            while ((row = mysql_fetch_row(result)))
            {
                MXS_DEBUG("wsrep_node_name name for %s is [%s]",
                          ptr->server->name,
                          row[1]);

                strncat(donor_list, row[1], DONOR_NODE_NAME_MAX_LEN);
                strcat(donor_list, ",");
            }

            mysql_free_result(result);
        }
        else
        {
            mon_report_query_error(ptr);
        }
    }

    int donor_list_size = strlen(donor_list);
    if (donor_list[donor_list_size - 1] == ',')
    {
        donor_list[donor_list_size - 1] = '\0';
    }

    strcat(donor_list, "\"");

    MXS_DEBUG("Sending %s to all slave nodes",
              donor_list);

    /* Set now rep_sst_donor in each slave node */
    for (unsigned int k = 0; k < found_slaves; k++)
    {
        MXS_MONITORED_SERVER *ptr = node_list[k];
        /* Set the Galera SST donor node list */
        if (mxs_mysql_query(ptr->con, donor_list) == 0)
        {
            MXS_DEBUG("SET GLOBAL rep_sst_donor OK in node %s",
                      ptr->server->name);
        }
        else
        {
            mon_report_query_error(ptr);
        }
    }

    MXS_FREE(donor_list);
}

/**
 * Compare routine for slave nodes sorted by 'wsrep_local_index'
 *
 * The default order is DESC.
 *
 * Nodes with lowest 'wsrep_local_index' value
 * are at the end of the list.
 *
 * @param   a        Pointer to array value
 * @param   b        Pointer to array value
 * @return  A number less than, threater than or equal to 0
 */

static int compare_node_index (const void *a, const void *b)
{
    const MXS_MONITORED_SERVER *s_a = *(MXS_MONITORED_SERVER * const *)a;
    const MXS_MONITORED_SERVER *s_b = *(MXS_MONITORED_SERVER * const *)b;

    // Order is DESC: b - a
    return s_b->server->node_id - s_a->server->node_id;
}

/**
 * Compare routine for slave nodes sorted by node priority
 *
 * The default order is DESC.
 *
 * Some special cases, i.e: no give priority, or 0 value
 * are handled.
 *
 * Note: the master selection algorithm is:
 * node with lowest priority value and > 0
 *
 * This sorting function will add master candidates
 * at the end of the list.
 *
 * @param   a        Pointer to array value
 * @param   b        Pointer to array value
 * @return  A number less than, threater than or equal to 0
 */

static int compare_node_priority (const void *a, const void *b)
{
    const MXS_MONITORED_SERVER *s_a = *(MXS_MONITORED_SERVER * const *)a;
    const MXS_MONITORED_SERVER *s_b = *(MXS_MONITORED_SERVER * const *)b;
    char pri_a[50];
    char pri_b[50];
    bool have_a = server_get_parameter_nolock(s_a->server, "priority", pri_a, sizeof(pri_a));
    bool have_b = server_get_parameter_nolock(s_b->server, "priority", pri_b, sizeof(pri_b));

    /**
     * Check priority parameter:
     *
     * Return a - b in case of issues
     */
    if (!have_a && have_b)
    {
        MXS_DEBUG("Server %s has no given priority. It will be at the beginning of the list",
                  s_a->server->name);
        return -(INT_MAX - 1);
    }
    else if (have_a && !have_b)
    {
        MXS_DEBUG("Server %s has no given priority. It will be at the beginning of the list",
                  s_b->server->name);
        return INT_MAX - 1;
    }
    else if (!have_a && !have_b)
    {
        MXS_DEBUG("Servers %s and %s have no given priority. They be at the beginning of the list",
                  s_a->server->name,
                  s_b->server->name);
        return 0;
    }

    /* The given  priority is valid */
    int pri_val_a = atoi(pri_a);
    int pri_val_b = atoi(pri_b);

    /* Return a - b in case of issues */
    if ((pri_val_a < INT_MAX && pri_val_a > 0) && !(pri_val_b < INT_MAX && pri_val_b > 0))
    {
        return pri_val_a;
    }
    else if (!(pri_val_a < INT_MAX && pri_val_a > 0) && (pri_val_b < INT_MAX && pri_val_b > 0))
    {
        return -pri_val_b;
    }
    else if (!(pri_val_a < INT_MAX && pri_val_a > 0) && !(pri_val_b < INT_MAX && pri_val_b > 0))
    {
        return 0;
    }

    // The order is DESC: b -a
    return pri_val_b - pri_val_a;
}

/**
 * When monitor starts all entries in hashable are deleted
 *
 * @param handle    The Galera specific data
 */
void GaleraMonitor::reset_cluster_info()
{
    int n_nodes = 0;
    HASHITERATOR *iterator;
    HASHTABLE *table = m_galera_nodes_info;
    void *key;

    /* Delete all entries in the hashtable */
    while ((iterator = hashtable_iterator(table)))
    {
        key = hashtable_next(iterator);
        if (!key)
        {
            break;
        }
        else
        {
            hashtable_iterator_free(iterator);
            hashtable_delete(table, key);
        }
    }
}

/**
 * Copy routine for hashtable values
 *
 * @param in    The nut data
 * @return      The copied data or NULL
 */
static GALERA_NODE_INFO *nodeval_dup(const GALERA_NODE_INFO *in)
{
    if (in == NULL ||
        in->cluster_size == 0 ||
        in->cluster_uuid == NULL ||
        in->node == NULL)
    {
        return NULL;
    }

    GALERA_NODE_INFO *rval = (GALERA_NODE_INFO *) MXS_CALLOC(1, sizeof(GALERA_NODE_INFO));
    char* uuid = MXS_STRDUP(in->cluster_uuid);

    if (!uuid || !rval)
    {
        MXS_FREE(rval);
        MXS_FREE(uuid);
        return NULL;
    }

    rval->cluster_uuid = uuid;
    rval->cluster_size = in->cluster_size;
    rval->local_index = in->local_index;
    rval->local_state = in->local_state;
    rval->node = in->node;
    rval->joined = in->joined;

    return rval;
}

/**
 * Free routine for hashtable values
 *
 * @param in    The data to be freed
 */
static void nodeval_free(GALERA_NODE_INFO *in)
{
    if (in)
    {
        MXS_FREE(in->cluster_uuid);
        MXS_FREE(in);
    }
}

/**
 * Detect possible cluster_uuid and cluster_size
 * in monitored nodes.
 * Set the cluster memebership in nodes
 * if a cluster can be set.
 *
 * @param mon The Monitor Instance
 */
void GaleraMonitor::set_galera_cluster()
{
    int ret = false;
    int n_nodes = 0;
    HASHITERATOR *iterator;
    HASHTABLE *table = m_galera_nodes_info;
    char *key;
    GALERA_NODE_INFO *value;
    int cluster_size = 0;
    char *cluster_uuid = NULL;

    /* Fetch all entries in the hashtable */
    if ((iterator = hashtable_iterator(table)) != NULL)
    {
        /* Get the Key */
        while ((key = static_cast<char*>(hashtable_next(iterator))) != NULL)
        {
            /* fetch the Value for the Key */
            value = static_cast<GALERA_NODE_INFO*>(hashtable_fetch(table, key));
            if (value)
            {
                if (!SERVER_IN_MAINT(value->node) &&
                    SERVER_IS_RUNNING(value->node) &&
                    value->joined)
                {
                    /* This server can be part of a cluster */
                    n_nodes++;

                    /* Set cluster_uuid for nodes that report
                     * highest value of cluster_size
                     */
                    if (value->cluster_size > cluster_size)
                    {
                        cluster_size = value->cluster_size;
                        cluster_uuid = value->cluster_uuid;
                    }

                    MXS_DEBUG("Candidate cluster member %s: UUID %s, joined nodes %d",
                              value->node->name,
                              value->cluster_uuid,
                              value->cluster_size);
                }
            }
        }

        hashtable_iterator_free(iterator);
    }

    /**
     * Detect if a possible cluster can
     * be set with n_nodes and cluster_size
     *
     * Special cases for n_nodes = 0 or 1.
     * If cluster_size > 1 there is rule
     */
    ret = detect_cluster_size(n_nodes,
                              cluster_uuid,
                              cluster_size);
    /**
     * Free && set the new cluster_uuid:
     * Handling the special case n_nodes == 1
     */
    if (ret || (!ret && n_nodes != 1))
    {
        /* Set the new cluster_uuid */
        MXS_FREE(m_cluster_info.c_uuid);
        m_cluster_info.c_uuid = ret ? MXS_STRDUP(cluster_uuid) : NULL;
        m_cluster_info.c_size = cluster_size;
    }

    /**
     * Set the JOINED status in cluster members only, if any.
     */
    set_cluster_members();
}

/**
 * Set the SERVER_JOINED in member nodes only
 *
 * Status bits SERVER_JOINED, SERVER_SLAVE, SERVER_MASTER
 * and SERVER_MASTER_STICKINESS are removed
 * in non member nodes.
 *
 * @param mon   The Monitor Instance
 */
void GaleraMonitor::set_cluster_members()
{
    GALERA_NODE_INFO *value;
    MXS_MONITORED_SERVER *ptr;
    char *c_uuid = m_cluster_info.c_uuid;
    int c_size = m_cluster_info.c_size;

    ptr = m_monitor->monitored_servers;
    while (ptr)
    {
        /* Fetch cluster info for this server, if any */
        value = static_cast<GALERA_NODE_INFO*>(hashtable_fetch(m_galera_nodes_info,
                                                               ptr->server->name));

        if (value && m_cluster_info.c_uuid)
        {
            /* Check whether this server is a candidate member */
            if (!SERVER_IN_MAINT(ptr->server) &&
                SERVER_IS_RUNNING(ptr->server) &&
                value->joined &&
                strcmp(value->cluster_uuid, c_uuid) == 0 &&
                value->cluster_size == c_size)
            {
                /* Server is member of current cluster */
                monitor_set_pending_status(ptr, SERVER_JOINED);
            }
            else
            {
                /* This server is not part of current cluster */
                monitor_clear_pending_status(ptr, SERVER_JOINED);
            }
        }
        else
        {
            /* This server is not member of any cluster */
            monitor_clear_pending_status(ptr, SERVER_JOINED);
        }

        /* Clear bits for non member nodes */
        if (!SERVER_IN_MAINT(ptr->server) && (!SERVER_IS_JOINED(ptr->server)))
        {
            ptr->server->depth = -1;
            ptr->server->node_id = -1;

            /* clear M/S status */
            monitor_clear_pending_status(ptr, SERVER_SLAVE);
            monitor_clear_pending_status(ptr, SERVER_MASTER);

            /* clear master sticky status */
            monitor_clear_pending_status(ptr, SERVER_MASTER_STICKINESS);
        }

        ptr = ptr->next;
    }
}

/**
 * Detect whether a Galer cluster can be set.
 *
 * @param handle          The Galera specific data
 * @param n_nodes         Nodes configured for this monitor
 * @param cluster_uuid    Possible cluster_uuid in nodes
 * @param cluster_size    Possible cluster_size in nodes
 * @return                True is a cluster can be set
 */
bool GaleraMonitor::detect_cluster_size(const int n_nodes,
                                        const char *candidate_uuid,
                                        const int candidate_size)
{
    bool ret = false;
    char *c_uuid = m_cluster_info.c_uuid;
    int c_size = m_cluster_info.c_size;

    /**
     * Decide whether we have a cluster
     */
    if (n_nodes == 0)
    {
        /* Log change if a previous UUID was set */
        if (c_uuid != NULL)
        {
            MXS_INFO("No nodes found to be part of a Galera cluster right now: aborting");
        }
    }
    else if (n_nodes == 1)
    {
        const char *msg = "Galera cluster with 1 node only";

        /* If 1 node only:
         * ifc_uuid is not set, return value will be true.
         * if c_uuid is equal to candidate_uuid, return value will be true.
         */
        if (c_uuid == NULL ||
            (c_uuid && strcmp(c_uuid, candidate_uuid) == 0))
        {
            ret = true;
        }

        /* Log change if no previous UUID was set */
        if (c_uuid == NULL)
        {
            if (ret)
            {
                MXS_INFO("%s has UUID %s: continue", msg, candidate_uuid);
            }
        }
        else
        {
            if (strcmp(c_uuid, candidate_uuid) && c_size != 1)
            {
                /* This error should be logged once */
                MXS_ERROR("%s and its UUID %s is different from previous set one %s: aborting",
                          msg,
                          candidate_uuid,
                          c_uuid);
            }
        }
    }
    else
    {
        int min_cluster_size = (n_nodes / 2) + 1;

        /* Return true if there are enough members */
        if (candidate_size >= min_cluster_size)
        {
            ret = true;
            /* Log the successful change once */
            if (c_uuid == NULL ||
                (c_uuid && strcmp(c_uuid, candidate_uuid)))
            {
                MXS_INFO("Galera cluster UUID is now %s with %d members of %d nodes",
                         candidate_uuid, candidate_size, n_nodes);
            }
        }
        else
        {
            if (!ret && c_uuid)
            {
                /* This error is being logged at every monitor cycle */
                MXS_ERROR("Galera cluster cannot be set with %d members of %d:"
                          " not enough nodes (%d at least)",
                          candidate_size, n_nodes, min_cluster_size);
            }
        }
    }

    return ret;
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    MXS_NOTICE("Initialise the MySQL Galera Monitor module.");

    static MXS_MODULE info =
    {
        MXS_MODULE_API_MONITOR,
        MXS_MODULE_GA,
        MXS_MONITOR_VERSION,
        "A Galera cluster monitor",
        "V2.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &maxscale::MonitorApi<GaleraMonitor>::s_api,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {"disable_master_failback", MXS_MODULE_PARAM_BOOL, "false"},
            {"available_when_donor", MXS_MODULE_PARAM_BOOL, "false"},
            {"disable_master_role_setting", MXS_MODULE_PARAM_BOOL, "false"},
            {"root_node_as_master", MXS_MODULE_PARAM_BOOL, "false"},
            {"use_priority", MXS_MODULE_PARAM_BOOL, "false"},
            {
                "script",
                MXS_MODULE_PARAM_PATH,
                NULL,
                MXS_MODULE_OPT_PATH_X_OK
            },
            {
                "events",
                MXS_MODULE_PARAM_ENUM,
                MXS_MONITOR_EVENT_DEFAULT_VALUE,
                MXS_MODULE_OPT_NONE,
                mxs_monitor_event_enum_values
            },
            {"set_donor_nodes", MXS_MODULE_PARAM_BOOL, "false"},
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
