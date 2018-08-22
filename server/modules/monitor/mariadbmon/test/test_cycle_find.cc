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

#include "../mariadbmon.hh"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <string>
#include <sstream>
#include <vector>
#include <maxbase/maxbase.hh>
#include <maxscale/alloc.h>
#include <set>

using std::string;
using std::cout;

// Maximum sizes for array types
const int MAX_CYCLE_SIZE = 10;
const int MAX_CYCLES = 5;
const int MAX_EDGES = 20;

class MariaDBMonitor::Test
{
    // These defines are required since Centos 6 doesn't support vector literal initialisers :/
    typedef struct
    {
        int members[MAX_CYCLE_SIZE];
    } CycleMembers;

    typedef struct
    {
        CycleMembers cycles[MAX_CYCLES];
    } CycleArray;

    typedef struct
    {
        int slave_id;
        int master_id;
    } Edge;

    typedef struct
    {
        Edge edges[MAX_EDGES];
    } EdgeArray;

public:
    Test();
    ~Test();
    int run_tests();
private:
    MariaDBMonitor* m_monitor;
    int m_current_test;

    void init_servers(int count);
    void clear_servers();
    void add_replication(EdgeArray edges);
    int check_result_cycles(CycleArray expected_cycles);
};

int main()
{
    maxbase::init();

    MariaDBMonitor::Test tester;
    return tester.run_tests();
}

MariaDBMonitor::Test::Test()
    : m_monitor(new MariaDBMonitor(NULL))
    , m_current_test(0)
{}

MariaDBMonitor::Test::~Test()
{
    delete m_monitor;
}

/**
 * Runs all the tests
 *
 * @return Number of failures
 */
int MariaDBMonitor::Test::run_tests()
{
    std::vector<int> results;

    // Test 1: 1 server, no replication
    init_servers(1);
    // No edges, no cycles
    results.push_back(check_result_cycles({}));

    // Test 2: 4 servers, two cycles with a connection between them
    init_servers(4);
    EdgeArray edges2 = {{{1,2},{2,1},{3,2},{3,4},{4,3}}};
    add_replication(edges2);
    CycleArray expected_cycles2 = {{{{1,2}},{{3,4}}}};
    results.push_back(check_result_cycles(expected_cycles2));

    // Test 3: 6 servers, with one cycle
    init_servers(6);
    EdgeArray edges3 = {{{2,1},{3,2},{4,3},{2,4},{5,1},{6,5},{6,4}}};
    add_replication(edges3);
    CycleArray expected_cycles3 = {{{{2,3,4}}}};
    results.push_back(check_result_cycles(expected_cycles3));

    // Test 4: 10 servers, with a big cycle composed of two smaller ones plus non-cycle servers
    init_servers(10);
    EdgeArray edges4 = {{{1,5},{2,1},{2,5},{3,1},{3,4},{3,10},{4,1},{5,6},{6,7},{6,4},{7,8},{8,6},{9,8}}};
    add_replication(edges4);
    CycleArray expected_cycles4 = {{{{1,5,6,7,8,4}}}};
    results.push_back(check_result_cycles(expected_cycles4));

    clear_servers();
    // Return total error count
    return std::accumulate(results.begin(), results.end(), 0);
}

/**
 * Add dummy servers, removing any existing ones.
 *
 * @param count How many servers to add. Server id:s will start from 1.
 */
void MariaDBMonitor::Test::init_servers(int count)
{
    clear_servers();
    mxb_assert(m_monitor->m_server_info.empty() && m_monitor->m_servers.empty() &&
               m_monitor->m_servers_by_id.empty());

    for (int i = 1; i < count + 1; i++)
    {
        SERVER* base_server = new SERVER; // Contents mostly undefined
        std::stringstream server_name;
        server_name << "Server" << i;
        base_server->name = MXS_STRDUP(server_name.str().c_str());
        MXS_MONITORED_SERVER* mon_server = new MXS_MONITORED_SERVER; // Contents mostly undefined
        mon_server->server = base_server;
        MariaDBServer* new_server = new MariaDBServer(mon_server, i - 1);
        new_server->m_server_id = i;
        m_monitor->m_servers.push_back(new_server);
        m_monitor->m_server_info[mon_server] = new_server;
        m_monitor->m_servers_by_id[i] = new_server;
    }
    m_current_test++;
}

/**
 * Clear dummy servers and free memory
 */
void MariaDBMonitor::Test::clear_servers()
{
    m_monitor->m_server_info.clear();
    m_monitor->m_servers_by_id.clear();
    for (auto iter = m_monitor->m_servers.begin(); iter != m_monitor->m_servers.end(); iter++)
    {
        MariaDBServer* server = *iter;
        MXS_FREE(server->m_server_base->server->name);
        delete server->m_server_base->server;
        delete server->m_server_base;
        delete server;
    }
    m_monitor->m_servers.clear();
}

/**
 * Add replication from slave to master
 *
 * @param edges An array of pairs of server id:s designating replication
 */
void MariaDBMonitor::Test::add_replication(EdgeArray edges)
{
    for (auto i = 0; i < MAX_EDGES; i++)
    {
        auto slave_id = edges.edges[i].slave_id;
        auto master_id = edges.edges[i].master_id;
        if (slave_id == 0 || master_id == 0)
        {
            break;
        }
        auto iter2 = m_monitor->m_servers_by_id.find(slave_id);
        mxb_assert(iter2 != m_monitor->m_servers_by_id.end());
        SlaveStatus ss;
        ss.master_server_id = master_id;
        ss.slave_io_running = SlaveStatus::SLAVE_IO_YES;
        (*iter2).second->m_slave_status.push_back(ss);
    }

    m_monitor->build_replication_graph();
    m_monitor->find_graph_cycles();
}

/**
 * Check that the nodes have cycles as is expected. Non-cycled nodes must have cycle = 0.
 *
 * @param expected_cycles An array of cycles. Each cycle is an array of server id:s designating the members
 * @return Number of failures
 */
int MariaDBMonitor::Test::check_result_cycles(CycleArray expected_cycles)
{
    std::stringstream test_name_ss;
    test_name_ss << "Test " << m_current_test << ": ";
    string test_name = test_name_ss.str();
    int errors = 0;
    // Copy the index->server map so it can be checked later
    IdToServerMap no_cycle_servers = m_monitor->m_servers_by_id;
    std::set<int> used_cycle_ids;
    for (auto iter = 0; iter < MAX_CYCLES; iter++)
    {
        int cycle_id = NodeData::CYCLE_NONE;
        CycleMembers cycle_member_ids = expected_cycles.cycles[iter];
        for (auto iter2 = 0; iter2 < MAX_CYCLE_SIZE; iter2++)
        {
            auto search_id = cycle_member_ids.members[iter2];
            if (search_id == 0)
            {
                break;
            }
            auto cycle_server = m_monitor->get_server(search_id);
            if (cycle_server->m_node.cycle == NodeData::CYCLE_NONE)
            {
                cout << test_name << cycle_server->name() << " is not in a cycle when it should.\n";
                errors++;
            }
            // If this is the first element, check what the cycle id should be for all members of the cycle.
            else if (cycle_id == NodeData::CYCLE_NONE)
            {
                cycle_id = cycle_server->m_node.cycle;
                if (used_cycle_ids.count(cycle_id) > 0)
                {
                    cout << test_name << cycle_server->name() << " is in unexpected cycle " <<
                        cycle_id << ".\n";
                    errors++;
                }
                else
                {
                    used_cycle_ids.insert(cycle_id);
                }
            }
            else if (cycle_server->m_node.cycle != cycle_id)
            {
                cout << test_name << cycle_server->name() << " is in cycle " << cycle_server->m_node.cycle <<
                    " when " << cycle_id << " was expected.\n";
                errors++;
            }
            no_cycle_servers.erase(cycle_server->m_server_id);
        }
    }

    // Check that servers not in expected_cycles are not in a cycle
    for (auto iter = no_cycle_servers.begin(); iter != no_cycle_servers.end(); iter++)
    {
        MariaDBServer* server = (*iter).second;
        if (server->m_node.cycle != NodeData::CYCLE_NONE)
        {
            cout << server->name() << " is in cycle " << server->m_node.cycle << " when none was expected.\n";
            errors++;
        }
    }

    return errors;
}
