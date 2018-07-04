/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mariadbmon.hh"
#include <inttypes.h>
#include <string>
#include <sstream>
#include <maxscale/modutil.h>
#include <maxscale/mysql_utils.h>
#include <algorithm>

using std::string;

static bool check_replicate_ignore_table(MXS_MONITORED_SERVER* database);
static bool check_replicate_do_table(MXS_MONITORED_SERVER* database);
static bool check_replicate_wild_do_table(MXS_MONITORED_SERVER* database);
static bool check_replicate_wild_ignore_table(MXS_MONITORED_SERVER* database);

static const char HB_TABLE_NAME[] = "maxscale_schema.replication_heartbeat";
static const int64_t MASTER_BITS = SERVER_MASTER | SERVER_WAS_MASTER;
static const int64_t SLAVE_BITS = SERVER_SLAVE | SERVER_WAS_SLAVE;


/**
 * Generic depth-first search. Iterates through child nodes (slaves) and runs the 'visit_func' on the nodes.
 * Isn't flexible enough for all uses.
 *
 * @param node Starting server. The server and all its slaves are visited.
 * @param data Caller-specific data, which is given to the 'visit_func'.
 * @param visit_func Function to run on a node when visiting it
 */
template <typename T>
void topology_DFS(MariaDBServer* node, T* data, void (*visit_func)(MariaDBServer* node, T* data))
{
   node->m_node.index = NodeData::INDEX_FIRST;
   if (visit_func)
   {
        visit_func(node, data);
   }
   for (auto iter = node->m_node.children.begin(); iter != node->m_node.children.end(); iter++)
   {
       MariaDBServer* slave = *iter;
       if (slave->m_node.index == NodeData::INDEX_NOT_VISITED)
       {
           topology_DFS<T>(slave, data, visit_func);
       }
   }
}

static bool server_config_compare(const MariaDBServer* lhs, const MariaDBServer* rhs)
{
    return lhs->m_config_index < rhs->m_config_index;
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
                        // Sort the cycle members according to monitor config order.
                        std::sort(members.begin(), members.end(), server_config_compare);
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
        (*iter)->m_node.reset_indexes();
        (*iter)->m_node.reset_results();
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

            /* IF THIS PART IS CHANGED, CHANGE THE COMPARISON IN 'sstatus_arrays_topology_equal'
             * (in MariaDBServer) accordingly so that any possible topology changes are detected. */
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
    m_cycles.clear();
    // The next items need to be passed around in the recursive calls to keep track of algorithm state.
    ServerArray stack;
    int index = NodeData::INDEX_FIRST; /* Node visit index */
    int cycle = NodeData::CYCLE_FIRST; /* If cycles are found, the nodes in the cycle are given an identical
                                        * cycle index. */

    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        /** Index is 0, this node has not yet been visited. */
        if ((*iter)->m_node.index == NodeData::INDEX_NOT_VISITED)
        {
            tarjan_scc_visit_node(*iter, &stack, &index, &cycle);
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
 * Find the server with the best reach in the candidates-array. Running state or 'read_only' is ignored by
 * this method.
 *
 * @param candidates Which servers to check. All servers in the array will have their 'reach' calculated
 * @return The best server out of the candidates
 */
MariaDBServer* MariaDBMonitor::find_best_reach_server(const ServerArray& candidates)
{
    ss_dassert(!candidates.empty());
    MariaDBServer* best_reach = NULL;
    /* Search for the server with the best reach. */
    for (auto iter = candidates.begin(); iter != candidates.end(); iter++)
    {
        MariaDBServer* candidate = *iter;
        calculate_node_reach(candidate);
        // This is the first valid node or this node has better reach than the so far best found ...
        if (best_reach == NULL || (candidate->m_node.reach > best_reach->m_node.reach))
        {
            best_reach = candidate;
        }
    }

    return best_reach;
}

static string disqualify_reasons_to_string(MariaDBServer* disqualified)
{
    string reasons;
    string separator;
    const string word_and = " and ";
    if (disqualified->is_in_maintenance())
    {
        reasons += separator + "in maintenance";
        separator = word_and;
    }
    if (disqualified->is_down())
    {
        reasons += separator + "down";
        separator = word_and;
    }
    if (disqualified->is_read_only())
    {
        reasons += separator + "in read_only mode";
    }
    return reasons;
}

/**
 * Find the best master server in the cluster. This method should only be called when the monitor
 * is starting, a cluster operation (e.g. failover) has occurred or the user has changed something on
 * the current master making it unsuitable. Because of this, the method can be quite vocal and not
 * consider the previous master.
 *
 * @param msg_out Message output. Includes explanations on why potential candidates were not selected.
 * @return The master with most slaves
 */
MariaDBServer* MariaDBMonitor::find_topology_master_server(string* msg_out)
{
    /* Finding the best master server may get somewhat tricky if the graph is complicated. The general
     * criteria for the best master is that it reaches the most slaves (possibly in multiple layers and
     * cycles). To avoid having to calculate this reachability (doable by a recursive search) to all nodes,
     * let's use the knowledge that the best master is either a server with no masters (external ones don't
     * count) or is part of a cycle with no out-cycle masters. The server must be running and writable
     * to be eligible. */
    string messages;
    string separator;
    const char disq[] = "is not a valid master candidate because it is ";
    ServerArray master_candidates;
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        MariaDBServer* server = *iter;
        if (server->m_node.parents.empty())
        {
            if (server->is_running() && !server->is_read_only())
            {
                master_candidates.push_back(server);
            }
            else
            {
                string reasons = disqualify_reasons_to_string(server);
                messages += separator + "'" + server->name() + "' " + disq + reasons + ".";
                separator = "\n";
            }
        }
    }

    // For each cycle, it's enough to take one sample server, as all members of a cycle have the same reach.
    for (auto iter = m_cycles.begin(); iter != m_cycles.end(); iter++)
    {
        int cycle_id = iter->first;
        ServerArray& cycle_members = m_cycles[cycle_id];
        // Check that no server in the cycle is replicating from outside the cycle. This requirement is
        // analogous with the same requirement for non-cycle servers.
        if (!cycle_has_master_server(cycle_members))
        {
            MariaDBServer* sample_server = find_master_inside_cycle(cycle_members);
            if (sample_server)
            {
                master_candidates.push_back(sample_server);
            }
            else
            {
                // No single server in the cycle was viable.
                const char no_valid_servers[] = "No valid master server could be found in the cycle with "
                                                "servers";
                string server_names = monitored_servers_to_string(cycle_members);
                messages += separator + no_valid_servers + " '" + server_names + "'.";
                separator = "\n";

                for (auto iter2 = cycle_members.begin(); iter2 != cycle_members.end(); iter2++)
                {
                    MariaDBServer* disqualified_server = *iter2;
                    string reasons = disqualify_reasons_to_string(disqualified_server);
                    messages += separator + "'" + disqualified_server->name() + "' " + disq + reasons + ".";
                    separator = "\n";
                }
            }
        }
    }

    *msg_out = messages;
    return master_candidates.empty() ? NULL : find_best_reach_server(master_candidates);
}

static void node_reach_visit(MariaDBServer* node, int* reach)
{
    *reach = *reach + 1;
}

/**
 * Calculate the total number of reachable child nodes for the given node. A node can always reach itself.
 * The result is saved into the node data.
 */
void MariaDBMonitor::calculate_node_reach(MariaDBServer* node)
{
    ss_dassert(node && node->m_node.reach == NodeData::REACH_UNKNOWN);
    // Reset indexes since they will be reused.
    reset_node_index_info();

    int reach = 0;
    topology_DFS<int>(node, &reach, node_reach_visit);
    node->m_node.reach = reach;
}

/**
 * Check which node in a cycle should be the master. The node must be running without read_only.
 *
 * @param cycle The cycle index
 * @return The selected node
 */
MariaDBServer* MariaDBMonitor::find_master_inside_cycle(ServerArray& cycle_members)
{
    /* For a cycle, all servers are equally good in a sense. The question is just if the server is up
     * and writable. */
    for (auto iter = cycle_members.begin(); iter != cycle_members.end(); iter++)
    {
        MariaDBServer* server = *iter;
        ss_dassert(server->m_node.cycle != NodeData::CYCLE_NONE);
        if (server->is_running() && !server->is_read_only())
        {
            return server;
        }
    }
    return NULL;
}

/**
 * Assign replication role status bits to the servers in the cluster. Starts from the cluster master server.
 */
void MariaDBMonitor::assign_master_and_slave()
{
    // Remove any existing [Master], [Slave] etc flags.
    const uint64_t remove_bits = SERVER_MASTER | SERVER_SLAVE | SERVER_RELAY_MASTER |
                                 SERVER_SLAVE_OF_EXT_MASTER;
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        (*iter)->clear_status(remove_bits);
    }

    // Check the the master node, label it as the [Master] if...
    if (m_master)
    {
        // the node has slaves, even if their slave sql threads are stopped ...
        if (!m_master->m_node.children.empty() ||
            // or detect standalone master is on ...
            m_detect_standalone_master ||
            // or "detect_stale_master" is on and the server was a master before.
            (m_detect_stale_master && (m_master->m_server_base->pending_status & SERVER_WAS_MASTER)))
        {
            m_master->clear_status(SLAVE_BITS | SERVER_RELAY_MASTER);
            if (m_master->has_status(SERVER_RUNNING))
            {
                m_master->set_status(MASTER_BITS);
            }
        }

        // Run another DFS, this time assigning slaves.
        reset_node_index_info();
        assign_slave_and_relay_master(m_master);
    }
}

/**
 * Check if the servers replicating from the given node qualify for [Slave] and mark them. Continue the
 * search to any found slaves.
 *
 * @param node The node to process. The node itself is not marked [Slave].
 */
void MariaDBMonitor::assign_slave_and_relay_master(MariaDBServer* node)
{
    ss_dassert(node->m_node.index == NodeData::INDEX_NOT_VISITED);
    node->m_node.index = NodeData::INDEX_FIRST;
    bool require_was_slave = false;

    if (node->is_down())
    {
        // If 'detect_stale_slave' is off, this node can only have slaves if the node is running.
        if (m_detect_stale_slave)
        {
            require_was_slave = true;
        }
        else
        {
            return;
        }
    }

    int slaves = 0;
    for (auto iter = node->m_node.children.begin(); iter != node->m_node.children.end(); iter++)
    {
        MariaDBServer* slave = *iter;
        // If the node has an index, it has already been labeled master/slave and visited. Even when this
        // is the case, the node has to be checked to get correct [Relay Master] labels.
        if (slave->m_node.index == NodeData::INDEX_NOT_VISITED)
        {
            slave->clear_status(MASTER_BITS);
        }
        // The slave node may have several slave connections, need to find the right one.
        bool found_slave_conn = false;
        for (auto iter2 = slave->m_slave_status.begin(); iter2 != slave->m_slave_status.end(); iter2++)
        {
            SlaveStatus& ss = *iter2;
            auto master_id = ss.master_server_id;
            auto io_running = ss.slave_io_running;
            // Should this check 'Master_Host' and 'Master_Port' instead of server id:s?
            if (master_id > 0 && master_id == node->m_server_id && ss.slave_sql_running &&
                (io_running == SlaveStatus::SLAVE_IO_YES ||
                 io_running == SlaveStatus::SLAVE_IO_CONNECTING) &&
                // Can in theory cause a 'SERVER_WAS_SLAVE' bit from another master to affect the result.
                (!require_was_slave || (slave->m_server_base->pending_status & SERVER_WAS_SLAVE)))
            {
                found_slave_conn = true;
                break;
            }
        }

        // If the slave had a valid connection, label it as a slave and recurse.
        if (found_slave_conn)
        {
            slaves++;
            if (slave->m_node.index == NodeData::INDEX_NOT_VISITED)
            {
                slave->clear_status(MASTER_BITS);
                if (slave->has_status(SERVER_RUNNING))
                {
                    slave->set_status(SLAVE_BITS);
                }
                assign_slave_and_relay_master(slave);
            }
        }
    }

    // Finally, if the node itself is a slave and has slaves of its own, label it as relay slave.
    if (node->has_status(SERVER_SLAVE) && slaves > 0)
    {
        node->set_status(SERVER_RELAY_MASTER);
    }
    // If the node is a binlog relay, remove any slave bits that may have been set. Relay master bit can stay.
    if (node->m_version == MariaDBServer::version::BINLOG_ROUTER)
    {
        node->clear_status(SERVER_SLAVE);
    }
}

/**
 * Is the current master server still valid or should a new one be selected?
 *
 * @param reason_out Output for a text description
 * @return True, if master is ok. False if the current master has changed in a way that
 * a new master should be selected.
 */
bool MariaDBMonitor::master_is_valid(std::string* reason_out)
{
    // The master server of the cluster needs to be re-calculated in the following four cases:
    bool rval = true;
    // 1) There is no master.
    if (m_master == NULL)
    {
        rval = false;
    }
    // 2) read_only has been activated on the master.
    else if (m_master->is_read_only())
    {
        rval = false;
        *reason_out = "it is in read-only mode";
    }
    // 3) The master was a non-replicating master (not in a cycle) but now has a slave connection.
    else if (m_master_cycle_status.cycle_id == NodeData::CYCLE_NONE)
    {
        // The master should not have a master of its own.
        if (!m_master->m_node.parents.empty())
        {
            rval = false;
            *reason_out = "it has started replicating from another server in the cluster";
        }
    }
    // 4) The master was part of a cycle but is no longer, or one of the servers in the cycle is
    //    replicating from a server outside the cycle.
    else
    {
        /* The master was previously in a cycle. Compare the current cycle to the previous data and see
         * if the cycle is still the best multimaster group. */
        int current_cycle_id = m_master->m_node.cycle;

        // 4a) The master is no longer in a cycle.
        if (current_cycle_id == NodeData::CYCLE_NONE)
        {
            rval = false;
            ServerArray& old_members = m_master_cycle_status.cycle_members;
            string server_names_old = monitored_servers_to_string(old_members);
            *reason_out = "it is no longer in the multimaster group (" + server_names_old + ")";
        }
        // 4b) The master is still in a cycle but the cycle has gained a master outside of the cycle.
        else
        {
            ServerArray& current_members = m_cycles[current_cycle_id];
            if (cycle_has_master_server(current_members))
            {
                rval = false;
                string server_names_current = monitored_servers_to_string(current_members);
                *reason_out = "a server in the master's multimaster group (" + server_names_current +
                    ") is replicating from a server not in the group";
            }
        }
    }
    return rval;
}

/**
 * Check if any of the servers in the cycle is replicating from a server not in the cycle. External masters
 * do not count.
 *
 * @param cycle The cycle to check
 * @return True if a server is replicating from a master not in the same cycle
 */
bool MariaDBMonitor::cycle_has_master_server(ServerArray& cycle_servers)
{
    bool outside_replication = false;
    int cycle_id = cycle_servers.front()->m_node.cycle;
    // Looks good, check that no cycle server is replicating from elsewhere.
    for (auto iter = cycle_servers.begin(); iter != cycle_servers.end() && !outside_replication; iter++)
    {
        MariaDBServer* server = *iter;
        for (auto iter_master = server->m_node.parents.begin();
             iter_master != server->m_node.parents.end();
             iter_master++)
        {
            if ((*iter_master)->m_node.cycle != cycle_id)
            {
                // Cycle member is replicating from a server that is not in the current cycle. The
                // cycle is not a valid "master" cycle.
                outside_replication = true;
                break;
            }
        }
    }

    return outside_replication;
}