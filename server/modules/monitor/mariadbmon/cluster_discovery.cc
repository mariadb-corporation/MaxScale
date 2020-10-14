/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mariadbmon.hh"

#include <algorithm>
#include <inttypes.h>
#include <string>
#include <queue>
#include <maxbase/format.hh>
#include <maxbase/host.hh>
#include <maxscale/modutil.hh>
#include <maxscale/mysql_utils.hh>

using std::string;
using maxbase::string_printf;

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
                                           ServerArray* stack,
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

/**
 * Use slave status and server id information to build the replication graph. Needs to be called whenever
 * topology has changed, or it's suspected.
 */
void MariaDBMonitor::build_replication_graph()
{
    const bool use_hostnames = m_settings.assume_unique_hostnames;
    // First, reset all node data.
    for (MariaDBServer* server : m_servers)
    {
        server->m_node.reset_indexes();
        server->m_node.reset_results();
    }

    for (auto slave : m_servers)
    {
        /* Check all slave connections of all servers. Connections are added even if one or both endpoints
         * are down or in maintenance. */
        for (auto& slave_conn : slave->m_slave_status)
        {
            slave_conn.master_server = nullptr;
            /* IF THIS PART IS CHANGED, CHANGE THE COMPARISON IN 'sstatus_arrays_topology_equal'
             * (in MariaDBServer) accordingly so that any possible topology changes are detected. */
            if (slave_conn.slave_io_running != SlaveStatus::SLAVE_IO_NO && slave_conn.slave_sql_running)
            {
                // Looks promising, check hostname or server id.
                MariaDBServer* found_master = NULL;
                bool is_external = false;
                if (use_hostnames)
                {
                    found_master = get_server(slave_conn.settings.master_endpoint);
                    if (!found_master)
                    {
                        // Must be an external server.
                        is_external = true;
                    }
                }
                else
                {
                    /* Cannot trust hostname:port since network may be complicated. Instead,
                     * trust the "Master_Server_Id"-field of the SHOW SLAVE STATUS output if
                     * the slave connection has been seen connected before. This means that
                     * the graph will miss slave-master relations that have not connected
                     * while the monitor has been running.
                     *
                     * TODO: This data should be saved so that monitor restarts do not lose
                     * this information. */
                    if (slave_conn.master_server_id >= 0 && slave_conn.seen_connected)
                    {
                        // Valid slave connection, find the MariaDBServer with the matching server id.
                        found_master = get_server(slave_conn.master_server_id);
                        if (!found_master)
                        {
                            /* Likely an external master. It's possible that the master is a monitored
                             * server which has not been queried yet and the monitor does not know its
                             * id. */
                            is_external = true;
                        }
                    }
                }

                // Valid slave connection, find the MariaDBServer with this id.
                if (found_master)
                {
                    /* Building the parents-array is not strictly required as the same data is in
                     * the children-array. This construction is more for convenience and faster
                     * access later on. */
                    slave->m_node.parents.push_back(found_master);
                    found_master->m_node.children.push_back(slave);
                    slave_conn.master_server = found_master;
                }
                else if (is_external)
                {
                    // This is an external master connection. Save just the master id for now.
                    // TODO: Save host:port instead
                    slave->m_node.external_masters.push_back(slave_conn.master_server_id);
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

/**
 * Find the best master server in the cluster. This method should only be called when the monitor
 * is starting, a cluster operation (e.g. failover) has occurred or the user has changed something on
 * the current master making it unsuitable. Because of this, the method can be quite vocal and not
 * consider the previous master.
 *
 * @param req_running Should non-running candidates be accepted
 * @param msg_out Message output. Includes explanations on why potential candidates were not selected.
 * @return The master with most slaves
 */
MariaDBServer* MariaDBMonitor::find_topology_master_server(RequireRunning req_running, string* msg_out)
{
    /* Finding the best master server may get somewhat tricky if the graph is complicated. The general
     * criteria for the best master is that it reaches the most slaves (possibly in multiple layers and
     * cycles). To avoid having to calculate this reachability (doable by a recursive search) to all nodes,
     * let's use the knowledge that the best master is either a server with no masters (external ones don't
     * count) or is part of a cycle with no out-cycle masters.
     *
     * Master candidates must be writable and not in maintenance to be eligible. Running candidates are
     * preferred. A downed candidate is accepted only if no current master exists. A downed master can
     * be failovered later. */

    ServerArray master_candidates;

    // Helper function for finding normal master candidates.
    auto search_outside_cycles = [this, &master_candidates](RequireRunning req_running,
                                                            DelimitedPrinter& topo_messages) {
        for (MariaDBServer* server : m_servers)
        {
            if (server->m_node.parents.empty())
            {
                string why_not;
                if (is_candidate_valid(server, req_running, &why_not))
                {
                    master_candidates.push_back(server);
                }
                else
                {
                    topo_messages.cat(why_not);
                }
            }
        }
    };

    // Helper function for finding master candidates inside cycles.
    auto search_inside_cycles = [this, &master_candidates](RequireRunning req_running,
                                                           DelimitedPrinter& topo_messages) {
        // For each cycle, it's enough to take one sample server, as all members of a cycle have the
        // same reach. The sample server needs to be valid, though.
        for (auto& iter : m_cycles)
        {
            ServerArray& cycle_members = iter.second;
            // Check that no server in the cycle is replicating from outside the cycle. This requirement is
            // analogous with the same requirement for non-cycle servers.
            if (!cycle_has_master_server(cycle_members))
            {
                // Find a valid candidate from the cycle.
                MariaDBServer* cycle_cand = nullptr;
                for (MariaDBServer* elem : cycle_members)
                {
                    mxb_assert(elem->m_node.cycle != NodeData::CYCLE_NONE);
                    if (is_candidate_valid(elem, req_running))
                    {
                        cycle_cand = elem;
                        break;
                    }
                }
                if (cycle_cand)
                {
                    master_candidates.push_back(cycle_cand);
                }
                else
                {
                    // No single server in the cycle was viable. Go through the cycle again and construct
                    // a message explaining why.
                    string server_names = monitored_servers_to_string(cycle_members);
                    string msg_start = string_printf("No valid master server could be found in the cycle with "
                                                     "servers %s:", server_names.c_str());
                    DelimitedPrinter cycle_invalid_msg("\n");
                    cycle_invalid_msg.cat(msg_start);
                    for (MariaDBServer* elem : cycle_members)
                    {
                        string server_msg;
                        is_candidate_valid(elem, req_running, &server_msg);
                        cycle_invalid_msg.cat(server_msg);
                    }
                    cycle_invalid_msg.cat(""); // Adds a linebreak
                    topo_messages.cat(cycle_invalid_msg.message());
                }
            }
        }
    };

    // Normally, do not accept downed servers as master.
    DelimitedPrinter topo_messages_reject_down("\n");
    search_outside_cycles(RequireRunning::REQUIRED, topo_messages_reject_down);
    search_inside_cycles(RequireRunning::REQUIRED, topo_messages_reject_down);

    MariaDBServer* best_candidate = nullptr;
    string messages;
    if (!master_candidates.empty())
    {
        // Found one or more candidates. Select the best one and output the messages.
        best_candidate = find_best_reach_server(master_candidates);
        messages = topo_messages_reject_down.message();
    }
    else if (req_running == RequireRunning::OPTIONAL)
    {
        // If no candidate was found and the caller allows, we get desperate and allow a downed server
        // to be selected. This is required for the case when MaxScale is started while the master is
        // already down. Failover may be able to fix the situation if settings allow it or
        // if activated manually.
        DelimitedPrinter topo_messages_accept_down("\n");
        search_outside_cycles(RequireRunning::OPTIONAL, topo_messages_accept_down);
        search_inside_cycles(RequireRunning::OPTIONAL, topo_messages_accept_down);
        if (!master_candidates.empty())
        {
            // Found one or more candidates which are down. Select the best one. Instead of the original
            // messages, output the messages without complaints that a server is down. The caller
            // should detect and explain that a non-running server was selected.
            best_candidate = find_best_reach_server(master_candidates);
            messages = topo_messages_accept_down.message();
        }
        else
        {
            // Still no luck. Output the messages from the first run since these explain if any potential
            // candidates were down.
            messages = topo_messages_reject_down.message();
        }
    }

    if (msg_out)
    {
        *msg_out = messages;
    }
    return best_candidate;
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
        server->m_replication_lag = SERVER::RLAG_UNDEFINED;
    }

    // Check the the master node, label it as the [Master] if
    // 1) the node has slaves, even if their slave sql threads are stopped
    // 2) or detect standalone master is on.
    if (m_master && (!m_master->m_node.children.empty() || m_settings.detect_standalone_master))
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
        else if (m_settings.detect_stale_master && (m_master->had_status(SERVER_WAS_MASTER)))
        {
            // The master is not running but it was the master last round and
            // may have running slaves who have up-to-date events.
            m_master->set_status(SERVER_WAS_MASTER);
        }

        // Run another graph search, this time assigning slaves.
        reset_node_index_info();
        assign_slave_and_relay_master(m_master);
    }

    if (!m_settings.ignore_external_masters)
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
    const bool allow_stale_slaves = m_settings.detect_stale_slave;

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
                        if (new_rlag != SERVER::RLAG_UNDEFINED
                            && (curr_rlag == SERVER::RLAG_UNDEFINED || new_rlag < curr_rlag))
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
        if (parent->m_srv_type == MariaDBServer::server_type::BINLOG_ROUTER)
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
        if (m_master->m_server_base->mon_err_count > m_settings.failcount && running_slaves(m_master) == 0)
        {
            rval = false;
            reason = string_printf("it has been down over %d (failcount) monitor updates and "
                                   "it does not have any running slaves",
                                   m_settings.failcount);
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
    if (m_cluster_topology_changed)
    {
        // Server IDs may have changed.
        m_servers_by_id.clear();
        for (auto server : m_servers)
        {
            if (server->m_server_id != SERVER_ID_UNKNOWN)
            {
                m_servers_by_id[server->m_server_id] = server;
            }
        }

        build_replication_graph();
        find_graph_cycles();
    }

    /* Check if a failover/switchover was performed last loop and the master should change.
     * In this case, update the master and its cycle info here. */
    if (m_next_master)
    {
        assign_new_master(m_next_master);
        m_next_master = NULL;
    }

    if (m_cluster_topology_changed || !m_master || !m_master->is_usable())
    {
        update_master();
    }

}
void MariaDBMonitor::update_master()
{
    // Check if current master is still valid.
    string reason_not_valid;
    bool current_is_ok = master_is_valid(&reason_not_valid);

    if (current_is_ok)
    {
        // Current master is still ok. If topology has changed, check which server looks like the best master
        // and alert if it's not the current master. Don't change yet.
        m_warn_current_master_invalid = true;
        if (m_cluster_topology_changed)
        {
            // Update master cycle info in case it has changed.
            update_master_cycle_info();
            MariaDBServer* master_cand = find_topology_master_server(RequireRunning::REQUIRED);
            // master_cand can be null if e.g. current master is down.
            if (master_cand && (master_cand != m_master))
            {
                // This is unlikely to be printed continuously because of the topology-change requirement.
                MXS_WARNING("'%s' is a better master candidate than the current master '%s'. "
                            "Master will change when '%s' is no longer a valid master.",
                            master_cand->name(), m_master->name(), m_master->name());
            }
        }
    }
    else if (m_master)
    {
        // Current master is faulty and swapping to a better, running server is allowed.
        string topology_messages;
        MariaDBServer* master_cand = find_topology_master_server(RequireRunning::REQUIRED,
                                                                 &topology_messages);
        m_warn_cannot_find_master = true;
        if (master_cand)
        {
            if (master_cand != m_master)
            {
                // We have another master to swap to. The messages give the impression that new master
                // selection has not yet happened, but this is just for clarity for the user.
                mxb_assert(!reason_not_valid.empty());
                MXS_WARNING("The current master server '%s' is no longer valid because %s. "
                            "Selecting new master server.",
                            m_master->name(), reason_not_valid.c_str());

                // At this point, print messages explaining why any/other possible master servers
                // weren't picked.
                if (!topology_messages.empty())
                {
                    MXS_WARNING("%s", topology_messages.c_str());
                }

                MXS_NOTICE("Setting '%s' as master.", master_cand->name());
                // Change the master, even though this may break replication.
                assign_new_master(master_cand);
            }
            else if (m_cluster_topology_changed)
            {
                // Tried to find another master but the current one is still the best. This is typically
                // caused by a topology change. The check on 'm_cluster_topology_changed' should stop this
                // message from printing repeatedly.
                MXS_WARNING("Attempted to find a replacement for the current master server '%s' because %s, "
                            "but '%s' is still the best master server.",
                            m_master->name(), reason_not_valid.c_str(), m_master->name());

                if (!topology_messages.empty())
                {
                    MXS_WARNING("%s", topology_messages.c_str());
                }
                // The following updates some data on the master.
                assign_new_master(master_cand);
            }
        }
        else if (m_warn_current_master_invalid)
        {
            // No alternative master. Keep current status and print warnings.
            // This situation may stick so only print the messages once.
            mxb_assert(!reason_not_valid.empty());
            MXS_WARNING("The current master server '%s' is no longer valid because %s, "
                        "but there is no valid alternative to swap to.",
                        m_master->name(), reason_not_valid.c_str());
            if (!topology_messages.empty())
            {
                MXS_WARNING("%s", topology_messages.c_str());
            }
            m_warn_current_master_invalid = false;
        }
    }
    else
    {
        // This happens only when starting from scratch before a master has been selected.
        // Accept either a running or downed master, preferring running servers.
        string topology_messages;
        MariaDBServer* master_cand = find_topology_master_server(RequireRunning::OPTIONAL,
                                                                 &topology_messages);
        if (master_cand)
        {
            MXS_NOTICE("Selecting new master server.");
            if (master_cand->is_down())
            {
                const char msg[] = "No running master candidates detected and no master currently set. "
                                   "Accepting a non-running server as master.";
                MXS_WARNING("%s", msg);
            }

            if (!topology_messages.empty())
            {
                MXS_WARNING("%s", topology_messages.c_str());
            }

            MXS_NOTICE("Setting '%s' as master.", master_cand->name());
            assign_new_master(master_cand);
        }
        else if (m_warn_cannot_find_master)
        {
            // No current master and could not select another. This situation may stick so only print once.
            MXS_WARNING("Tried to find a master but no valid master server found.");
            if (!topology_messages.empty())
            {
                MXS_WARNING("%s", topology_messages.c_str());
            }
            m_warn_cannot_find_master = false;
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
            MXS_WARNING("Setting '%s' to maintenance because it is low on disk space.", server->name());
            server->set_status(SERVER_MAINT);
        }
    }
}

bool MariaDBMonitor::is_candidate_valid(MariaDBServer* cand, RequireRunning req_running, string* why_not)
{
    bool is_valid = true;
    DelimitedPrinter reasons(" and ");
    if (cand->is_in_maintenance())
    {
        is_valid = false;
        reasons.cat("in maintenance");
    }

    if (cand->is_read_only())
    {
        is_valid = false;
        reasons.cat("in read_only mode");
    }

    if (req_running == RequireRunning::REQUIRED && cand->is_down())
    {
        is_valid = false;
        reasons.cat("down");
    }

    if (!is_valid && why_not)
    {
        *why_not = string_printf("'%s' is not a valid master candidate because it is %s.",
                                 cand->name(), reasons.message().c_str());
    }
    return is_valid;
};

MariaDBMonitor::DNSResolver::StringSet MariaDBMonitor::DNSResolver::resolve_server(const string& host)
{
    auto now = mxb::Clock::now();
    const auto MAX_AGE = mxb::Duration((double)5*60); // Refresh interval for cache entries.
    auto recent_time = now - MAX_AGE;
    DNSResolver::StringSet rval;

    auto iter = m_mapping.find(host);
    if (iter == m_mapping.end() || iter->second.timestamp < recent_time)
    {
        // Map did not have a record, or it was too old. In either case, generate a new one.
        DNSResolver::StringSet addresses;
        string error_msg;
        bool dns_success = mxb::name_lookup(host, &addresses, &error_msg);
        if (!dns_success)
        {
            MXB_ERROR("Could not resolve host '%s'. %s", host.c_str(), error_msg.c_str());
        }
        // If dns failed, the array will be empty. Add/replace the element anyway to prevent repeated lookups.
        m_mapping[host] = {addresses, now};
        rval = std::move(addresses);
    }
    else
    {
        // Return recent value.
        rval = iter->second.addresses;
    }
    return rval;
}
