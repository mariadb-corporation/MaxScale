/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include "mariadbmon.hh"
#include <inttypes.h>
#include <maxscale/modutil.h>
#include <maxscale/mysql_utils.h>

static bool check_replicate_ignore_table(MXS_MONITORED_SERVER* database);
static bool check_replicate_do_table(MXS_MONITORED_SERVER* database);
static bool check_replicate_wild_do_table(MXS_MONITORED_SERVER* database);
static bool check_replicate_wild_ignore_table(MXS_MONITORED_SERVER* database);

static const char HB_TABLE_NAME[] = "maxscale_schema.replication_heartbeat";

/**
 * This function computes the replication tree from a set of monitored servers and returns the root server
 * with SERVER_MASTER bit. The tree is computed even for servers in 'maintenance' mode.
 *
 * @return The server at root level with SERVER_MASTER bit
 */
MXS_MONITORED_SERVER* MariaDBMonitor::get_replication_tree()
{
    const int num_servers = m_servers.size();
    MXS_MONITORED_SERVER *ptr;
    MXS_MONITORED_SERVER *backend;
    SERVER *current;
    int depth = 0;
    long node_id;
    int root_level;

    ptr = m_monitor->monitored_servers;
    root_level = num_servers;

    while (ptr)
    {
        /* The server could be in SERVER_IN_MAINT
         * that means SERVER_IS_RUNNING returns 0
         * Let's check only for SERVER_IS_DOWN: server is not running
         */
        if (get_server_info(ptr)->is_down())
        {
            ptr = ptr->next;
            continue;
        }
        depth = 0;
        current = ptr->server;

        node_id = current->master_id;

        /** Either this node doesn't replicate from a master or the master
         * where it replicates from is not configured to this monitor. */
        if (node_id < 1 ||
            getServerByNodeId(node_id) == NULL)
        {
            MXS_MONITORED_SERVER *find_slave;
            find_slave = getSlaveOfNodeId(current->node_id, ACCEPT_DOWN);

            if (find_slave == NULL)
            {
                current->depth = -1;
                ptr = ptr->next;

                continue;
            }
            else
            {
                current->depth = 0;
            }
        }
        else
        {
            depth++;
        }

        while (depth <= num_servers)
        {
            /* set the root master at lowest depth level */
            if (current->depth > -1 && current->depth < root_level)
            {
                root_level = current->depth;
                m_master = get_server_info(ptr);
            }
            backend = getServerByNodeId(node_id);

            if (backend)
            {
                node_id = backend->server->master_id;
            }
            else
            {
                node_id = -1;
            }

            if (node_id > 0)
            {
                current->depth = depth + 1;
                depth++;

            }
            else
            {
                MXS_MONITORED_SERVER *master_cand;
                current->depth = depth;

                master_cand = getServerByNodeId(current->master_id);
                if (master_cand && master_cand->server && master_cand->server->node_id > 0)
                {
                    master_cand->server->depth = current->depth - 1;

                    if (m_master && master_cand->server->depth < m_master->m_server_base->server->depth)
                    {
                        /** A master with a lower depth was found, remove
                            the master status from the previous master. */
                        monitor_clear_pending_status(m_master->m_server_base, SERVER_MASTER);
                        m_master = get_server_info(master_cand);
                    }

                    MariaDBServer* info = get_server_info(master_cand);
                    if (info->is_running())
                    {
                        /** Only set the Master status if read_only is disabled */
                        monitor_set_pending_status(master_cand,
                                                   info->m_read_only ? SERVER_SLAVE : SERVER_MASTER);
                    }
                }
                else
                {
                    if (current->master_id > 0)
                    {
                        monitor_set_pending_status(ptr, SERVER_SLAVE);
                        monitor_set_pending_status(ptr, SERVER_SLAVE_OF_EXT_MASTER);
                    }
                }
                break;
            }

        }

        ptr = ptr->next;
    }

    /*
     * Return the root master
     */

    if (m_master != NULL)
    {
        /* If the root master is in MAINT, return NULL */
        if (m_master->is_in_maintenance())
        {
            return NULL;
        }
        else
        {
            return m_master->m_server_base;
        }
    }
    else
    {
        return NULL;
    }
}

/**
 * Fetch a node by node_id
 *
 * @param node_id The server_id to fetch
 *
 * @return The server with the required server_id
 */
MXS_MONITORED_SERVER* MariaDBMonitor::getServerByNodeId(long node_id)
{
    SERVER *current;
    MXS_MONITORED_SERVER *ptr = m_monitor->monitored_servers;
    while (ptr)
    {
        current = ptr->server;
        if (current->node_id == node_id)
        {
            return ptr;
        }
        ptr = ptr->next;
    }
    return NULL;
}

/**
 * Fetch a slave node from a node_id
 *
 * @param node_id            The server_id to fetch
 * @param slave_down_setting Whether to accept or reject slaves which are down
 * @return                   The slave server of this node_id
 */
MXS_MONITORED_SERVER* MariaDBMonitor::getSlaveOfNodeId(long node_id, slave_down_setting_t slave_down_setting)
{
    MXS_MONITORED_SERVER *ptr = m_monitor->monitored_servers;
    SERVER *current;
    while (ptr)
    {
        current = ptr->server;
        if (current->master_id == node_id &&
            (slave_down_setting == ACCEPT_DOWN || !get_server_info(ptr)->is_down()))
        {
            return ptr;
        }
        ptr = ptr->next;
    }
    return NULL;
}

/**
 * @brief Visit a node in the graph
 *
 * This function is the main function used to determine whether the node is a part of a cycle. It is
 * an implementation of the Tarjan's strongly connected component algorithm. All one node cycles are
 * ignored since normal master-slave monitoring handles that.
 *
 * Tarjan's strongly connected component algorithm:
 * https://en.wikipedia.org/wiki/Tarjan%27s_strongly_connected_components_algorithm
 *
 * @param node Target server/node
 * @param stack The stack used by the algorithm, contains nodes which have not yet been assigned a cycle
 * @param next_ind Visitation index of next node
 * @param next_cycle Index of next found cycle
 */
void MariaDBMonitor::tarjan_scc_visit_node(MariaDBServer *node, ServerArray* stack,
                                           int *next_ind, int *next_cycle)
{
    /** Assign an index to this node */
    NodeData& node_info = node->m_node;
    auto ind = *next_ind;
    node_info.index = ind;
    node_info.lowest_index = ind;
    *next_ind = ind + 1;

    if (node_info.parents.empty())
    {
        /* This node/server does not replicate from any node, it can't be a part of a cycle. Don't even
         * bother pushing it to the stack. */
    }
    else
    {
        // Has master servers, need to investigate.
        stack->push_back(node);
        node_info.in_stack = true;

        for (auto iter = node_info.parents.begin(); iter != node_info.parents.end(); iter++)
        {
            NodeData& parent_node = (*iter)->m_node;
            if (parent_node.index == NodeData::INDEX_NOT_VISITED)
            {
                /** Node has not been visited, so recurse. */
                tarjan_scc_visit_node((*iter), stack, next_ind, next_cycle);
                node_info.lowest_index = MXS_MIN(node_info.lowest_index, parent_node.lowest_index);
            }
            else if (parent_node.in_stack)
            {
                /* The parent node has been visited and is still on the stack. We have a cycle. */
                node_info.lowest_index = MXS_MIN(node_info.lowest_index, parent_node.index);
            }

            /* If parent_node.active==false, the parent has been visited, but is not in the current stack.
             * This means that while there is a route from this node to the parent, there is no route
             * from the parent to this node. No cycle. */
        }

        /* At the end of a visit to node, leave this node on the stack if it has a path to a node earlier
         * on the stack (index > lowest_index). Otherwise, start popping elements. */
        if (node_info.index == node_info.lowest_index)
        {
            int cycle_size = 0; // Keep track of cycle size since we don't mark one-node cycles.
            auto cycle_ind = *next_cycle;
            while (true)
            {
                ss_dassert(!stack->empty());
                MariaDBServer* cycle_server = stack->back();
                NodeData& cycle_node = cycle_server->m_node;
                stack->pop_back();
                cycle_node.in_stack = false;
                cycle_size++;
                if (cycle_node.index == node_info.index) // Last node in cycle
                {
                    if (cycle_size > 1)
                    {
                        cycle_node.cycle = cycle_ind;
                        ServerArray& members = m_cycles[cycle_ind]; // Creates array if didn't exist
                        members.push_back(cycle_server);
                        // All cycle elements popped. Next cycle...
                        *next_cycle = cycle_ind + 1;
                    }
                    break;
                }
                else
                {
                    cycle_node.cycle = cycle_ind; // Has more nodes, mark cycle.
                    ServerArray& members = m_cycles[cycle_ind];
                    members.push_back(cycle_server);
                }
            }
        }
    }
}

void MariaDBMonitor::build_replication_graph()
{
    // First, reset all node data.
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        (*iter)->m_node.reset();
    }

    /* Here, all slave connections are added to the graph, even if the IO thread cannot connect. Strictly
     * speaking, building the parents-array is not required as the data already exists. This construction
     * is more for convenience and faster access later on. */
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        /* All servers are accepted in this loop, even if the server is [Down] or [Maintenance]. For these
         * servers, we just use the latest available information. Not adding such servers could suddenly
         * change the topology quite a bit and all it would take is a momentarily network failure. */
        MariaDBServer* slave = *iter;

        for (auto iter_ss = slave->m_slave_status.begin(); iter_ss != slave->m_slave_status.end();
             iter_ss++)
        {
            SlaveStatus& slave_conn = *iter_ss;
            /* We always trust the "Master_Server_Id"-field of the SHOW SLAVE STATUS output, as long as
             * the id is > 0 (server uses 0 for default). This means that the graph constructed is faulty if
             * an old "Master_Server_Id"- value is read from a slave which is still trying to connect to
             * a new master. However, a server is only designated [Slave] if both IO- and SQL-threads are
             * running fine, so the faulty graph does not cause wrong status settings. */
            auto master_id = slave_conn.master_server_id;
            if (slave_conn.slave_io_running != SlaveStatus::SLAVE_IO_NO && master_id > 0)
            {
                // Valid slave connection, find the MariaDBServer with this id.
                auto master = get_server(master_id);
                if (master != NULL)
                {
                    slave->m_node.parents.push_back(master);
                    master->m_node.children.push_back(slave);
                }
                else
                {
                    // This is an external master connection. Save just the master id for now.
                    slave->m_node.external_masters.push_back(master_id);
                }
            }
        }
    }
}

/**
 * @brief Find the strongly connected components in the replication tree graph
 *
 * Each replication cluster is a directed graph made out of replication
 * trees. If this graph has strongly connected components (more generally
 * cycles), it is considered a multi-master cluster due to the fact that there
 * are multiple nodes where the data can originate.
 *
 * Detecting the cycles in the graph allows this monitor to better understand
 * the relationships between the nodes. All nodes that are a part of a cycle can
 * be labeled as master nodes. This information will later be used to choose the
 * right master where the writes should go.
 *
 * This function also populates the MYSQL_SERVER_INFO structures group
 * member. Nodes in a group get a positive group ID where the nodes not in a
 * group get a group ID of 0.
 */
void MariaDBMonitor::find_graph_cycles()
{
    build_replication_graph();
    m_cycles.clear();
    // The next items need to be passed around in the recursive calls to keep track of algorithm state.
    ServerArray stack;
    int index = 1; // Node visit index.
    int cycle = 1; // If cycles are found, the nodes in the cycle are given an identical cycle index.

    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        /** Index is 0, this node has not yet been visited. */
        if ((*iter)->m_node.index == NodeData::INDEX_NOT_VISITED)
        {
            tarjan_scc_visit_node(*iter, &stack, &index, &cycle);
        }
    }

    assign_cycle_roles(cycle);
}

void MariaDBMonitor::assign_cycle_roles(int cycle)
{
    // TODO: This part needs to be rewritten as it's faulty. And moved elsewhere.
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        MariaDBServer& server = **iter;
        MXS_MONITORED_SERVER* mon_srv = server.m_server_base;
        if (server.m_node.cycle > 0)
        {
            /** We have at least one cycle in the graph */
            if (server.m_read_only)
            {
                monitor_set_pending_status(mon_srv, SERVER_SLAVE | SERVER_WAS_SLAVE);
                monitor_clear_pending_status(mon_srv, SERVER_MASTER);
            }
            else
            {
                monitor_set_pending_status(mon_srv, SERVER_MASTER);
                monitor_clear_pending_status(mon_srv, SERVER_SLAVE | SERVER_WAS_SLAVE);
            }
        }
        else if (m_detect_stale_master && cycle == 1 && mon_srv->mon_prev_status & SERVER_MASTER &&
                 (mon_srv->pending_status & SERVER_MASTER) == 0)
        {
            /**
             * Stale master detection is handled here for multi-master mode.
             *
             * If we know that no cycles were found from the graph and that a
             * server once had the master status, replication has broken
             * down. These masters are assigned the stale master status allowing
             * them to be used as masters even if they lose their slaves. A
             * slave in this case can be either a normal slave or another
             * master.
             */
            if (server.m_read_only)
            {
                /** The master is in read-only mode, set it into Slave state */
                monitor_set_pending_status(mon_srv, SERVER_SLAVE | SERVER_WAS_SLAVE);
                monitor_clear_pending_status(mon_srv, SERVER_MASTER | SERVER_WAS_MASTER);
            }
            else
            {
                monitor_set_pending_status(mon_srv, SERVER_MASTER | SERVER_WAS_MASTER);
                monitor_clear_pending_status(mon_srv, SERVER_SLAVE | SERVER_WAS_SLAVE);
            }
        }
    }
}

/**
 * Check if the maxscale_schema.replication_heartbeat table is replicated on all
 * servers and log a warning if problems were found.
 *
 * @param monitor Monitor structure
 */
void MariaDBMonitor::check_maxscale_schema_replication()
{
    MXS_MONITORED_SERVER* database = m_monitor->monitored_servers;
    bool err = false;

    while (database)
    {
        mxs_connect_result_t rval = mon_ping_or_connect_to_db(m_monitor, database);
        if (mon_connection_is_ok(rval))
        {
            if (!check_replicate_ignore_table(database) ||
                !check_replicate_do_table(database) ||
                !check_replicate_wild_do_table(database) ||
                !check_replicate_wild_ignore_table(database))
            {
                err = true;
            }
        }
        else
        {
            mon_log_connect_error(database, rval);
        }
        database = database->next;
    }

    if (err)
    {
        MXS_WARNING("Problems were encountered when checking if '%s' is replicated. Make sure that "
                    "the table is replicated to all slaves.", HB_TABLE_NAME);
    }
}

/**
 * Check if replicate_ignore_table is defined and if maxscale_schema.replication_hearbeat
 * table is in the list.
 * @param database Server to check
 * @return False if the table is not replicated or an error occurred when querying
 * the server
 */
static bool check_replicate_ignore_table(MXS_MONITORED_SERVER* database)
{
    MYSQL_RES *result;
    bool rval = true;

    if (mxs_mysql_query(database->con,
                        "show variables like 'replicate_ignore_table'") == 0 &&
        (result = mysql_store_result(database->con)) &&
        mysql_num_fields(result) > 1)
    {
        MYSQL_ROW row;

        while ((row = mysql_fetch_row(result)))
        {
            if (strlen(row[1]) > 0 &&
                strcasestr(row[1], HB_TABLE_NAME))
            {
                MXS_WARNING("'replicate_ignore_table' is "
                            "defined on server '%s' and '%s' was found in it. ",
                            database->server->name, HB_TABLE_NAME);
                rval = false;
            }
        }

        mysql_free_result(result);
    }
    else
    {
        MXS_ERROR("Failed to query server %s for "
                  "'replicate_ignore_table': %s",
                  database->server->name,
                  mysql_error(database->con));
        rval = false;
    }
    return rval;
}

/**
 * Check if replicate_do_table is defined and if maxscale_schema.replication_hearbeat
 * table is not in the list.
 * @param database Server to check
 * @return False if the table is not replicated or an error occurred when querying
 * the server
 */
static bool check_replicate_do_table(MXS_MONITORED_SERVER* database)
{
    MYSQL_RES *result;
    bool rval = true;

    if (mxs_mysql_query(database->con,
                        "show variables like 'replicate_do_table'") == 0 &&
        (result = mysql_store_result(database->con)) &&
        mysql_num_fields(result) > 1)
    {
        MYSQL_ROW row;

        while ((row = mysql_fetch_row(result)))
        {
            if (strlen(row[1]) > 0 &&
                strcasestr(row[1], HB_TABLE_NAME) == NULL)
            {
                MXS_WARNING("'replicate_do_table' is "
                            "defined on server '%s' and '%s' was not found in it. ",
                            database->server->name, HB_TABLE_NAME);
                rval = false;
            }
        }
        mysql_free_result(result);
    }
    else
    {
        MXS_ERROR("Failed to query server %s for "
                  "'replicate_do_table': %s",
                  database->server->name,
                  mysql_error(database->con));
        rval = false;
    }
    return rval;
}

/**
 * Check if replicate_wild_do_table is defined and if it doesn't match
 * maxscale_schema.replication_heartbeat.
 * @param database Database server
 * @return False if the table is not replicated or an error occurred when trying to
 * query the server.
 */
static bool check_replicate_wild_do_table(MXS_MONITORED_SERVER* database)
{
    MYSQL_RES *result;
    bool rval = true;

    if (mxs_mysql_query(database->con,
                        "show variables like 'replicate_wild_do_table'") == 0 &&
        (result = mysql_store_result(database->con)) &&
        mysql_num_fields(result) > 1)
    {
        MYSQL_ROW row;

        while ((row = mysql_fetch_row(result)))
        {
            if (strlen(row[1]) > 0)
            {
                mxs_pcre2_result_t rc = modutil_mysql_wildcard_match(row[1], HB_TABLE_NAME);
                if (rc == MXS_PCRE2_NOMATCH)
                {
                    MXS_WARNING("'replicate_wild_do_table' is "
                                "defined on server '%s' and '%s' does not match it. ",
                                database->server->name,
                                HB_TABLE_NAME);
                    rval = false;
                }
            }
        }
        mysql_free_result(result);
    }
    else
    {
        MXS_ERROR("Failed to query server %s for "
                  "'replicate_wild_do_table': %s",
                  database->server->name,
                  mysql_error(database->con));
        rval = false;
    }
    return rval;
}

/**
 * Check if replicate_wild_ignore_table is defined and if it matches
 * maxscale_schema.replication_heartbeat.
 * @param database Database server
 * @return False if the table is not replicated or an error occurred when trying to
 * query the server.
 */
static bool check_replicate_wild_ignore_table(MXS_MONITORED_SERVER* database)
{
    MYSQL_RES *result;
    bool rval = true;

    if (mxs_mysql_query(database->con,
                        "show variables like 'replicate_wild_ignore_table'") == 0 &&
        (result = mysql_store_result(database->con)) &&
        mysql_num_fields(result) > 1)
    {
        MYSQL_ROW row;

        while ((row = mysql_fetch_row(result)))
        {
            if (strlen(row[1]) > 0)
            {
                mxs_pcre2_result_t rc = modutil_mysql_wildcard_match(row[1], HB_TABLE_NAME);
                if (rc == MXS_PCRE2_MATCH)
                {
                    MXS_WARNING("'replicate_wild_ignore_table' is "
                                "defined on server '%s' and '%s' matches it. ",
                                database->server->name,
                                HB_TABLE_NAME);
                    rval = false;
                }
            }
        }
        mysql_free_result(result);
    }
    else
    {
        MXS_ERROR("Failed to query server %s for "
                  "'replicate_wild_do_table': %s",
                  database->server->name,
                  mysql_error(database->con));
        rval = false;
    }
    return rval;
}

/**
 * @brief Check whether standalone master conditions have been met
 *
 * This function checks whether all the conditions to use a standalone master are met. For this to happen,
 * only one server must be available and other servers must have passed the configured tolerance level of
 * failures.
 *
 * @return True if standalone master should be used
 */
bool MariaDBMonitor::standalone_master_required()
{
    int candidates = 0;
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        MariaDBServer* server = *iter;
        if (server->is_running())
        {
            candidates++;
            if (server->m_read_only || !server->m_slave_status.empty() || candidates > 1)
            {
                return false;
            }
        }
        else if (server->m_server_base->mon_err_count < m_failcount)
        {
            return false;
        }
    }

    return candidates == 1;
}

/**
 * @brief Use standalone master
 *
 * This function assigns the last remaining server the master status and sets all other servers into
 * maintenance mode. By setting the servers into maintenance mode, we prevent any possible conflicts when
 * the failed servers come back up.
 *
 * @return True if standalone master was set
 */
bool MariaDBMonitor::set_standalone_master()
{
    bool rval = false;
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        MariaDBServer* server = *iter;
        auto mon_server = server->m_server_base;
        if (server->is_running())
        {
            if (!server->is_master() && m_warn_set_standalone_master)
            {
                MXS_WARNING("Setting standalone master, server '%s' is now the master.%s",
                            server->name(), m_allow_cluster_recovery ? "" :
                            " All other servers are set into maintenance mode.");
                m_warn_set_standalone_master = false;
            }

            monitor_set_pending_status(mon_server, SERVER_MASTER | SERVER_WAS_MASTER);
            monitor_clear_pending_status(mon_server, SERVER_SLAVE);
            m_master = server;
            rval = true;
        }
        else if (!m_allow_cluster_recovery)
        {
            server->set_status(SERVER_MAINT);
        }
    }

    return rval;
}

/**
 * Compute replication tree, find root master.
 *
 * @return Found master server or NULL
 */
MariaDBServer* MariaDBMonitor::find_root_master()
{
    MXS_MONITORED_SERVER* found_root_master = NULL;
    const int num_servers = m_servers.size();
    /* if only one server is configured, that's is Master */
    if (num_servers == 1)
    {
        auto mon_server = m_servers[0]->m_server_base;
        if (m_servers[0]->is_running())
        {
            mon_server->server->depth = 0;
            /* status cleanup */
            monitor_clear_pending_status(mon_server, SERVER_SLAVE);
            /* master status set */
            monitor_set_pending_status(mon_server, SERVER_MASTER);

            mon_server->server->depth = 0;
            m_master = m_servers[0];
            found_root_master = mon_server;
        }
    }
    else
    {
        /* Compute the replication tree */
        found_root_master = get_replication_tree();
    }

    if (m_detect_multimaster && num_servers > 0)
    {
        /** Find all the master server cycles in the cluster graph. If
         multiple masters are found, the servers with the read_only
         variable set to ON will be assigned the slave status. */
        find_graph_cycles();
    }

    return found_root_master ? get_server_info(found_root_master) : NULL;
}

/**
 * Test if server is a relay master and assign status if yes.
 *
 * @param candidate The server to assign
 */
void MariaDBMonitor::assign_relay_master(MariaDBServer& candidate)
{
    MXS_MONITORED_SERVER* ptr = candidate.m_server_base;
    if (ptr->server->node_id > 0 && ptr->server->master_id > 0 &&
        getSlaveOfNodeId(ptr->server->node_id, REJECT_DOWN) &&
        getServerByNodeId(ptr->server->master_id) &&
        (!m_detect_multimaster || candidate.m_node.cycle == 0))
    {
        /** This server is both a slave and a master i.e. a relay master */
        monitor_set_pending_status(ptr, SERVER_RELAY_MASTER);
        monitor_clear_pending_status(ptr, SERVER_MASTER);
    }
}

/**
 * Update serve states of a single server
 *
 * @param db_server Server to update
 * @param root_master_server The current best master
 */
void MariaDBMonitor::update_server_states(MariaDBServer& db_server, MariaDBServer* root_master_server)
{
    MXS_MONITORED_SERVER* root_master = root_master_server ? root_master_server->m_server_base : NULL;
    if (!db_server.is_in_maintenance())
    {
        /** If "detect_stale_master" option is On, let's use the previous master.
         *
         * Multi-master mode detects the stale masters in find_graph_cycles().
         *
         * TODO: If a stale master goes down and comes back up, it loses
         * the master status. An adequate solution would be to promote
         * the stale master as a real master if it is the last running server.
         */
        MXS_MONITORED_SERVER* ptr = db_server.m_server_base;
        if (m_detect_stale_master && root_master && !m_detect_multimaster &&
            // This server is still the root master and ...
            (strcmp(ptr->server->address, root_master->server->address) == 0 &&
             ptr->server->port == root_master->server->port) &&
            // had master status but is now losing it.
            (ptr->mon_prev_status & SERVER_MASTER) && !(ptr->pending_status & SERVER_MASTER) &&
            !db_server.m_read_only)
        {
            db_server.set_status(SERVER_WAS_MASTER | SERVER_MASTER);

            /** Log the message only if the master server didn't have
             * the stale master bit set */
            if ((ptr->mon_prev_status & SERVER_WAS_MASTER) == 0)
            {
                MXS_WARNING("All slave servers under the current master server have been lost. "
                            "Assigning Stale Master status to the old master server '%s' (%s:%i).",
                            ptr->server->name, ptr->server->address,
                            ptr->server->port);
            }
        }

        if (m_detect_stale_slave)
        {
            uint64_t bits = SERVER_SLAVE | SERVER_RUNNING;

            if ((ptr->mon_prev_status & bits) == bits &&
                root_master && SRV_MASTER_STATUS(root_master->pending_status))
            {
                /** Slave with a running master, assign stale slave candidacy */
                if ((ptr->pending_status & bits) == bits)
                {
                    monitor_set_pending_status(ptr, SERVER_WAS_SLAVE);
                }
                /** Server lost slave when a master is available, remove
                 * stale slave candidacy */
                else if ((ptr->pending_status & bits) == SERVER_RUNNING)
                {
                    monitor_clear_pending_status(ptr, SERVER_WAS_SLAVE);
                }
            }
            /** If this server was a stale slave candidate, assign
             * slave status to it */
            else if (ptr->mon_prev_status & SERVER_WAS_SLAVE &&
                     ptr->pending_status & SERVER_RUNNING &&
                     // Master is down
                     (!root_master || !SRV_MASTER_STATUS(root_master->pending_status) ||
                      // Master just came up
                      (SRV_MASTER_STATUS(root_master->pending_status) &&
                       (root_master->mon_prev_status & SERVER_MASTER) == 0)))
            {
                monitor_set_pending_status(ptr, SERVER_SLAVE);
            }
            else if (root_master == NULL && !db_server.m_slave_status.empty())
            {
                monitor_set_pending_status(ptr, SERVER_SLAVE);
            }
        }
    }
}
