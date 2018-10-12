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
#include <queue>
#include <maxscale/modutil.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/utils.hh>

using std::string;
using maxscale::string_printf;

namespace
{
using VisitorFunc = std::function<bool (MariaDBServer*)>;   // Used by graph search

/**
 * Generic depth-first search. Iterates through the root and its child nodes (slaves) and runs
 * 'visitor' on the nodes. 'NodeData::reset_indexes()' should be ran before this function
 * depending on if previous node visits should be omitted or not.
 *
 * @param root Starting server. The server and all its slaves are visited.
 * @param visitor Function to run on a node when visiting it. If it returns true,
 * the search is continued to the children of the node.
 */
void topology_DFS(MariaDBServer* root, VisitorFunc& visitor)
{
    int next_index = NodeData::INDEX_FIRST;
    // This lambda is recursive, so its type needs to be defined and it needs to "capture itself".
    std::function<void(MariaDBServer*, VisitorFunc&)> topology_DFS_visit =
        [&topology_DFS_visit, &next_index](MariaDBServer* node, VisitorFunc& visitor) {
            mxb_assert(node->m_node.index == NodeData::INDEX_NOT_VISITED);
            node->m_node.index = next_index++;
            if (visitor(node))
            {
                for (MariaDBServer* slave : node->m_node.children)
                {
                    if (slave->m_node.index == NodeData::INDEX_NOT_VISITED)
                    {
                        topology_DFS_visit(slave, visitor);
                    }
                }
            }
        };

    topology_DFS_visit(root, visitor);
}
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
void MariaDBMonitor::tarjan_scc_visit_node(MariaDBServer* node,
                                           ServerArray*   stack,
                                           int* next_ind,
                                           int* next_cycle)
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

        for (MariaDBServer* parent : node_info.parents)
        {
            NodeData& parent_node = parent->m_node;
            if (parent_node.index == NodeData::INDEX_NOT_VISITED)
            {
                /** Node has not been visited, so recurse. */
                tarjan_scc_visit_node(parent, stack, next_ind, next_cycle);
                node_info.lowest_index = MXS_MIN(node_info.lowest_index, parent_node.lowest_index);
            }
            else if (parent_node.in_stack)
            {
                /* The parent node has been visited and is still on the stack. We have a cycle. */
                node_info.lowest_index = MXS_MIN(node_info.lowest_index, parent_node.index);
            }

            /* The else-clause here can be omitted, since in that case the parent has been visited,
             * but is not in the current stack. This means that while there is a route from this
             * node to the parent, there is no route from the parent to this node. No cycle. */
        }

        /* At the end of a visit to node, leave this node on the stack if it has a path to a node earlier
         * on the stack (index > lowest_index). Otherwise, start popping elements. */
        if (node_info.index == node_info.lowest_index)
        {
            int cycle_size = 0;     // Keep track of cycle size since we don't mark one-node cycles.
            auto cycle_ind = *next_cycle;
            while (true)
            {
                mxb_assert(!stack->empty());
                MariaDBServer* cycle_server = stack->back();
                NodeData& cycle_node = cycle_server->m_node;
                stack->pop_back();
                cycle_node.in_stack = false;
                cycle_size++;
                if (cycle_node.index == node_info.index)    // Last node in cycle
                {
                    if (cycle_size > 1)
                    {
                        cycle_node.cycle = cycle_ind;
                        ServerArray& members = m_cycles[cycle_ind];     // Creates array if didn't exist
                        members.push_back(cycle_server);
                        // Sort the cycle members according to monitor config order.
                        std::sort(members.begin(), members.end(),
                                  [](const MariaDBServer* lhs, const MariaDBServer* rhs) -> bool {
                                      return lhs->m_config_index < rhs->m_config_index;
                                  });
                        // All cycle elements popped. Next cycle...
                        *next_cycle = cycle_ind + 1;
                    }
                    break;
                }
                else
                {
                    cycle_node.cycle = cycle_ind;   // Has more nodes, mark cycle.
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
    for (MariaDBServer* server : m_servers)
    {
        server->m_node.reset_indexes();
        server->m_node.reset_results();
    }

    /* Here, all slave connections are added to the graph, even if the IO thread cannot connect. Strictly
     * speaking, building the parents-array is not required as the data already exists. This construction
     * is more for convenience and faster access later on. */
    for (MariaDBServer* slave : m_servers)
    {
        /* All servers are accepted in this loop, even if the server is [Down] or [Maintenance]. For these
         * servers, we just use the latest available information. Not adding such servers could suddenly
         * change the topology quite a bit and all it would take is a momentarily network failure. */

        for (SlaveStatus& slave_conn : slave->m_slave_status)
        {
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
    int index = NodeData::INDEX_FIRST;      /* Node visit index */
    int cycle = NodeData::CYCLE_FIRST;      /* If cycles are found, the nodes in the cycle are given an
                                             * identical
                                             * cycle index. */

    for (MariaDBServer* server : m_servers)
    {
        /** Index is 0, this node has not yet been visited. */
        if (server->m_node.index == NodeData::INDEX_NOT_VISITED)
        {
            tarjan_scc_visit_node(server, &stack, &index, &cycle);
        }
    }
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
    mxb_assert(!candidates.empty());
    MariaDBServer* best_reach = NULL;
    /* Search for the server with the best reach. */
    for (MariaDBServer* candidate : candidates)
    {
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
    for (MariaDBServer* server : m_servers)
    {
        if (server->m_node.parents.empty())
        {
            if (server->is_usable() && !server->is_read_only())
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
    for (auto& iter : m_cycles)
    {
        ServerArray& cycle_members = iter.second;
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

                for (MariaDBServer* disqualified_server : cycle_members)
                {
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

/**
 * Calculate the total number of reachable child (slave) nodes for the given node. A
 * node can reach itself if it's running. Slaves are counted if they are running.
 * The result is saved into the node data.
 *
 * @param search_root Start point of the search
 */
void MariaDBMonitor::calculate_node_reach(MariaDBServer* search_root)
{
    mxb_assert(search_root && search_root->m_node.reach == NodeData::REACH_UNKNOWN);
    // Reset indexes since they will be reused.
    reset_node_index_info();

    int reach = 0;
    VisitorFunc visitor = [&reach](MariaDBServer* node) -> bool {
            bool node_running = node->is_running();
            if (node_running)
            {
                reach++;
            }
            // The node is expanded if it's running.
            return node_running;
        };

    topology_DFS(search_root, visitor);
    search_root->m_node.reach = reach;
}

/**
 * Calculate the total number of running slaves that the node has. The node itself can be down.
 * Slaves are counted even if they are connected through an inactive relay.
 *
 * @param node The node to calculate for
 * @return The number of running slaves
 */
int MariaDBMonitor::running_slaves(MariaDBServer* search_root)
{
    // Reset indexes since they will be reused.
    reset_node_index_info();

    int n_running_slaves = 0;
    VisitorFunc visitor = [&n_running_slaves](MariaDBServer* node) -> bool {
            if (node->is_running())
            {
                n_running_slaves++;
            }
            // The node is always expanded.
            return true;
        };

    topology_DFS(search_root, visitor);
    return n_running_slaves;
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
    for (MariaDBServer* server : cycle_members)
    {
        mxb_assert(server->m_node.cycle != NodeData::CYCLE_NONE);
        if (server->is_usable() && !server->is_read_only())
        {
            return server;
        }
    }
    return NULL;
}

/**
 * Assign replication role status bits to the servers in the cluster. Starts from the cluster master server.
 * Also updates replication lag.
 */
void MariaDBMonitor::assign_server_roles()
{
    // Remove any existing [Master], [Slave] etc flags from 'pending_status', they are still available in
    // 'mon_prev_status'.
    const uint64_t remove_bits = SERVER_MASTER | SERVER_WAS_MASTER | SERVER_SLAVE | SERVER_RELAY
        | SERVER_SLAVE_OF_EXT_MASTER;
    for (auto server : m_servers)
    {
        server->clear_status(remove_bits);
        server->m_replication_lag = MXS_RLAG_UNDEFINED;
    }

    // Check the the master node, label it as the [Master] if
    // 1) the node has slaves, even if their slave sql threads are stopped
    // 2) or detect standalone master is on.
    if (m_master && (!m_master->m_node.children.empty() || m_detect_standalone_master))
    {
        if (m_master->is_running())
        {
            // Master gets replication lag 0 even if it's replicating from an external server.
            m_master->m_replication_lag = 0;
            if (m_master->is_read_only())
            {
                // Special case: read_only is ON on a running master but there is no alternative master.
                // In this case, label the master as a slave and proceed normally.
                m_master->set_status(SERVER_SLAVE);
            }
            else
            {
                // Master is running and writable.
                m_master->set_status(SERVER_MASTER | SERVER_WAS_MASTER);
            }
        }
        else if (m_detect_stale_master && (m_master->had_status(SERVER_WAS_MASTER)))
        {
            // The master is not running but it was the master last round and
            // may have running slaves who have up-to-date events.
            m_master->set_status(SERVER_WAS_MASTER);
        }

        // Run another graph search, this time assigning slaves.
        reset_node_index_info();
        assign_slave_and_relay_master(m_master);
    }

    if (!m_ignore_external_masters)
    {
        // Do a sweep through all the nodes in the cluster (even the master) and mark external slaves.
        for (MariaDBServer* server : m_servers)
        {
            if (!server->m_node.external_masters.empty())
            {
                server->set_status(SERVER_SLAVE_OF_EXT_MASTER);
            }
        }
    }
}

/**
 * Check if the servers replicating from the given node qualify for [Slave] and mark them. Continue the
 * search to any found slaves. Also updates replication lag.
 *
 * @param start_node The root master node where the search begins. The node itself is not marked [Slave].
 */
void MariaDBMonitor::assign_slave_and_relay_master(MariaDBServer* start_node)
{
    mxb_assert(start_node->m_node.index == NodeData::INDEX_NOT_VISITED);
    // Combines a node with its connection state. The state tracks whether there is a series of
    // running slave connections all the way to the master server. If even one server is down or
    // a connection is broken in the series, the link is considered stale.
    struct QueueElement
    {
        MariaDBServer* node;
        bool           active_link;
    };

    auto compare = [](const QueueElement& left, const QueueElement& right) {
            return !left.active_link && right.active_link;
        };
    /* 'open_set' contains the nodes which the search should expand to. It's a priority queue so that nodes
     * with a functioning chain of slave connections to the master are processed first. Only after all such
     * nodes have been processed does the search expand to downed or disconnected nodes. */
    std::priority_queue<QueueElement, std::vector<QueueElement>, decltype(compare)> open_set(compare);

    // Begin by adding the starting node to the open_set. Then keep running until no more nodes can be found.
    QueueElement start = {start_node, start_node->is_running()};
    open_set.push(start);
    int next_index = NodeData::INDEX_FIRST;
    const bool allow_stale_slaves = m_detect_stale_slave;

    while (!open_set.empty())
    {
        auto parent = open_set.top().node;
        // If the node is not running or does not have an active link to master,
        // it can only have "stale slaves". Such slaves are assigned if
        // the slave connection has been observed to have worked before.
        bool parent_has_live_link = open_set.top().active_link && !parent->is_down();
        open_set.pop();

        if (parent->m_node.index != NodeData::INDEX_NOT_VISITED)
        {
            // This node has already been processed and can be skipped. The same node
            // can be in the open set multiple times if it has multiple slave connections.
            continue;
        }
        else
        {
            parent->m_node.index = next_index++;
        }

        bool has_running_slaves = false;
        for (MariaDBServer* slave : parent->m_node.children)
        {
            // If the slave has an index, it has already been visited and labelled master/slave.
            // Even when this is the case, the node has to be checked to get correct
            // [Relay Master] labels.

            // Need to differentiate between stale and running slave connections.
            bool found_slave_conn = false;  // slave->parent connection exists
            bool conn_is_live = false;      // live connection chain slave->cluster_master exists
            auto sstatus = slave->slave_connection_status(parent);
            if (sstatus)
            {
                if (sstatus->slave_io_running == SlaveStatus::SLAVE_IO_YES)
                {
                    found_slave_conn = true;
                    // Would it be possible to have the parent down while IO is still connected?
                    // Perhaps, if the slave is slow to update the connection status.
                    conn_is_live = parent_has_live_link && slave->is_running();
                }
                else if (sstatus->slave_io_running == SlaveStatus::SLAVE_IO_CONNECTING)
                {
                    found_slave_conn = true;
                }
            }

            // If the slave had a valid connection, label it as a slave and add it to the open set if not
            // yet visited.
            if (found_slave_conn && (conn_is_live || allow_stale_slaves))
            {
                bool slave_is_running = slave->is_running();
                if (slave_is_running)
                {
                    has_running_slaves = true;
                }
                if (slave->m_node.index == NodeData::INDEX_NOT_VISITED)
                {
                    // Add the slave server to the priority queue to a position depending on the master
                    // link status. It will be expanded later in the loop.
                    open_set.push({slave, conn_is_live});

                    // The slave only gets the slave flags if it's running.
                    // TODO: If slaves with broken links should be given different flags, add that here.
                    if (slave_is_running)
                    {
                        slave->set_status(SERVER_SLAVE);
                        // Write the replication lag for this slave. It may have multiple slave connections,
                        // in which case take the smallest value. This only counts the slave connections
                        // leading to the master or a relay.
                        int curr_rlag = slave->m_replication_lag;
                        int new_rlag = sstatus->seconds_behind_master;
                        if (new_rlag != MXS_RLAG_UNDEFINED
                            && (curr_rlag == MXS_RLAG_UNDEFINED || new_rlag < curr_rlag))
                        {
                            slave->m_replication_lag = new_rlag;
                        }
                    }
                }
            }
        }

        // Finally, if the node itself is a running slave and has slaves of its own, label it as relay.
        if (parent != m_master && parent_has_live_link
            && parent->has_status(SERVER_SLAVE | SERVER_RUNNING) && has_running_slaves)
        {
            parent->set_status(SERVER_RELAY);
        }
        // If the node is a binlog relay, remove any slave bits that may have been set.
        // Relay master bit can stay.
        if (parent->m_version == MariaDBServer::version::BINLOG_ROUTER)
        {
            parent->clear_status(SERVER_SLAVE);
        }
    }
}

/**
 * Is the current master server still valid or should a new one be selected?
 *
 * @param reason_out If master is not valid, the reason is printed here.
 * @return True, if master is ok. False if the current master has changed in a way that
 * a new master should be selected.
 */
bool MariaDBMonitor::master_is_valid(std::string* reason_out)
{
    bool rval = true;
    string reason;
    // The master server of the cluster needs to be re-calculated in the following cases:

    // 1) There is no master. This typically only applies when MaxScale is first ran.
    if (m_master == NULL)
    {
        rval = false;
    }
    // 2) read_only has been activated on the master.
    else if (m_master->is_read_only())
    {
        rval = false;
        reason = "it is in read-only mode";
    }
    // 3) The master has been down for more than failcount iterations and there is no hope of any kind of
    //    failover fixing the situation. The master is a hopeless one if it has been down for a while and
    //    has no running slaves, not even behind relays.
    //
    //    This condition should account for the situation when a dba or another MaxScale performs a failover
    //    and moves all the running slaves under another master. If even one running slave remains, the switch
    //    will not happen.
    else if (m_master->is_down())
    {
        // These two conditionals are separate since cases 4&5 should not apply if master is down.
        if (m_master->m_server_base->mon_err_count > m_failcount && running_slaves(m_master) == 0)
        {
            rval = false;
            reason = string_printf("it has been down over %d (failcount) monitor updates and "
                                   "it does not have any running slaves",
                                   m_failcount);
        }
    }
    // 4) The master was a non-replicating master (not in a cycle) but now has a slave connection.
    else if (m_master_cycle_status.cycle_id == NodeData::CYCLE_NONE)
    {
        // The master should not have a master of its own.
        if (!m_master->m_node.parents.empty())
        {
            rval = false;
            reason = "it has started replicating from another server in the cluster";
        }
    }
    // 5) The master was part of a cycle but is no longer, or one of the servers in the cycle is
    //    replicating from a server outside the cycle.
    else
    {
        /* The master was previously in a cycle. Compare the current cycle to the previous data and see
         * if the cycle is still the best multimaster group. */
        int current_cycle_id = m_master->m_node.cycle;

        // 5a) The master is no longer in a cycle.
        if (current_cycle_id == NodeData::CYCLE_NONE)
        {
            rval = false;
            ServerArray& old_members = m_master_cycle_status.cycle_members;
            string server_names_old = monitored_servers_to_string(old_members);
            reason = "it is no longer in the multimaster group (" + server_names_old + ")";
        }
        // 5b) The master is still in a cycle but the cycle has gained a master outside of the cycle.
        else
        {
            ServerArray& current_members = m_cycles[current_cycle_id];
            if (cycle_has_master_server(current_members))
            {
                rval = false;
                string server_names_current = monitored_servers_to_string(current_members);
                reason = "a server in the master's multimaster group (" + server_names_current
                    + ") is replicating from a server not in the group";
            }
        }
    }

    *reason_out = reason;
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
    mxb_assert(!cycle_servers.empty());
    bool outside_replication = false;
    int cycle_id = cycle_servers.front()->m_node.cycle;

    for (MariaDBServer* server : cycle_servers)
    {
        for (MariaDBServer* master : server->m_node.parents)
        {
            if (master->m_node.cycle != cycle_id)
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

void MariaDBMonitor::update_topology()
{
    m_servers_by_id.clear();
    for (auto server : m_servers)
    {
        m_servers_by_id[server->m_server_id] = server;
    }
    build_replication_graph();
    find_graph_cycles();

    /* Check if a failover/switchover was performed last loop and the master should change.
     * In this case, update the master and its cycle info here. */
    if (m_next_master)
    {
        assign_new_master(m_next_master);
        m_next_master = NULL;
    }

    // Find the server that looks like it would be the best master. It does not yet overwrite the
    // current master.
    string topology_messages;
    MariaDBServer* master_candidate = find_topology_master_server(&topology_messages);
    // If the 'master_candidate' is a valid server but different from the current master,
    // a change may be necessary. It will only happen if the current master is no longer usable.
    bool have_better = (master_candidate && master_candidate != m_master);
    bool current_still_best = (master_candidate && master_candidate == m_master);

    // Check if current master is still valid.
    string reason_not_valid;
    bool current_is_ok = master_is_valid(&reason_not_valid);

    if (current_is_ok)
    {
        m_warn_current_master_invalid = true;
        // Update master cycle info in case it has changed.
        update_master_cycle_info();
        if (have_better)
        {
            // Master is still valid but it is no longer the best master. Print a warning. This
            // may be a continuous situation so only print once.
            if (m_warn_have_better_master)
            {
                MXS_WARNING("'%s' is a better master candidate than the current master '%s'. "
                            "Master will change when '%s' is no longer a valid master.",
                            master_candidate->name(),
                            m_master->name(),
                            m_master->name());
                m_warn_have_better_master = false;
            }
        }
    }
    else
    {
        // Current master is faulty or does not exist
        m_warn_have_better_master = true;
        if (have_better)
        {
            // We have an alternative. Swap master. The messages give the impression
            // that new master selection has not yet happened, but this is just for clarity.
            const char sel_new_master[] = "Selecting new master server.";
            if (m_master)
            {
                mxb_assert(!reason_not_valid.empty());
                MXS_WARNING("The current master server '%s' is no longer valid because %s. %s",
                            m_master->name(),
                            reason_not_valid.c_str(),
                            sel_new_master);
            }
            else
            {
                // This typically happens only when starting from scratch.
                MXS_NOTICE("%s", sel_new_master);
            }

            // At this point, print messages explaining why any/other possible master servers weren't picked.
            if (!topology_messages.empty())
            {
                MXS_WARNING("%s", topology_messages.c_str());
            }

            MXS_NOTICE("Setting '%s' as master.", master_candidate->name());
            // Change the master, even though this may break replication.
            assign_new_master(master_candidate);
        }
        else if (current_still_best)
        {
            // Tried to find another master but the current one is still the best.
            MXS_WARNING("Attempted to find a replacement for the current master server '%s' because %s, "
                        "but '%s' is still the best master server.",
                        m_master->name(),
                        reason_not_valid.c_str(),
                        m_master->name());

            if (!topology_messages.empty())
            {
                MXS_WARNING("%s", topology_messages.c_str());
            }
            // The following updates some data on the master.
            assign_new_master(master_candidate);
        }
        else
        {
            // No alternative master. Keep current status and print warnings.
            // This situation may stick so only print the messages once.
            if (m_warn_current_master_invalid)
            {
                if (m_master)
                {
                    mxb_assert(!reason_not_valid.empty());
                    MXS_WARNING("The current master server '%s' is no longer valid because %s, "
                                "but there is no valid alternative to swap to.",
                                m_master->name(),
                                reason_not_valid.c_str());
                }
                else
                {
                    MXS_WARNING("No valid master server found.");
                }

                if (!topology_messages.empty())
                {
                    MXS_WARNING("%s", topology_messages.c_str());
                }
                m_warn_current_master_invalid = false;
            }
        }
    }
}

void MariaDBMonitor::set_low_disk_slaves_maintenance()
{
    // Only set pure slave and standalone servers to maintenance.
    for (MariaDBServer* server : m_servers)
    {
        if (server->is_low_on_disk_space() && server->is_usable()
            && !server->is_master() && !server->is_relay_master())
        {
            // TODO: Handle relays somehow, e.g. switch with a slave
            MXS_WARNING("Setting %s to maintenance because it is low on disk space.", server->name());
            server->set_status(SERVER_MAINT);
        }
    }
}
