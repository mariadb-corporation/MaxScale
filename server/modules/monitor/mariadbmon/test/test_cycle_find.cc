/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "../mariadbmon.hh"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <set>
#include <string>
#include <vector>
#include <maxbase/log.hh>
#include <maxbase/maxbase.hh>
#include <maxbase/alloc.h>
#include <maxscale/mainworker.hh>
#include "../../../../core/internal/server.hh"

using std::string;
using std::cout;
using maxscale::MonitorServer;

// Maximum sizes for array types
const int MAX_CYCLE_SIZE = 10;
const int MAX_CYCLES = 5;
const int MAX_EDGES = 20;

class MariaDBMonitor::Test : public Monitor::Test
{
    // TODO: Now using C++11 even on Centos 6 so get rid of these
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
    explicit Test(bool use_hostnames);
    int run_tests();
private:
    int  m_current_test = 0;
    bool m_use_hostnames = true;

    MariaDBMonitor* monitor() const;
    void            init_servers(int count);
    void            clear_servers();
    void            add_replication(EdgeArray edges);
    int             check_result_cycles(CycleArray expected_cycles);

    string         create_hostname(int id);
    MariaDBServer* get_server(int id);
};

int main(int argc, char** argv)
{
    mxs::Config::init(argc, argv);
    maxbase::init();
    maxbase::Log log;
    mxb::WatchdogNotifier notifier(0);
    mxs::MainWorker mw(&notifier);

    bool use_hostnames = true;
    MariaDBMonitor::Test tester1(use_hostnames);
    int rval = tester1.run_tests();

    use_hostnames = false;
    MariaDBMonitor::Test tester2(use_hostnames);
    rval += tester2.run_tests();
    return rval;
}

MariaDBMonitor::Test::Test(bool use_hostnames)
    : Monitor::Test(new MariaDBMonitor("TestMonitor", MXS_MODULE_NAME))
    , m_use_hostnames(use_hostnames)
{
}

MariaDBMonitor* MariaDBMonitor::Test::monitor() const
{
    return static_cast<MariaDBMonitor*>(m_monitor.get());
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
    EdgeArray edges2 = {{{1, 2}, {2, 1}, {3, 2}, {3, 4}, {4, 3}}};
    add_replication(edges2);
    CycleArray expected_cycles2 = {{{{1, 2}}, {{3, 4}}}};
    results.push_back(check_result_cycles(expected_cycles2));

    // Test 3: 6 servers, with one cycle
    init_servers(6);
    EdgeArray edges3 = {{{2, 1}, {3, 2}, {4, 3}, {2, 4}, {5, 1}, {6, 5}, {6, 4}}};
    add_replication(edges3);
    CycleArray expected_cycles3 = {{{{2, 3, 4}}}};
    results.push_back(check_result_cycles(expected_cycles3));

    // Test 4: 10 servers, with a big cycle composed of two smaller ones plus non-cycle servers
    init_servers(10);
    EdgeArray edges4 =
    {   {{1, 5}, {2, 1}, {2, 5}, {3, 1}, {3, 4}, {3, 10}, {4, 1}, {5, 6}, {6, 7}, {6, 4}, {7, 8},
        {8, 6},
        {9, 8}}};
    add_replication(edges4);
    CycleArray expected_cycles4 = {{{{1, 5, 6, 7, 8, 4}}}};
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
    monitor()->m_settings.assume_unique_hostnames = m_use_hostnames;
    mxb_assert(m_monitor->servers().empty() && monitor()->m_servers_by_id.empty());

    for (int i = 0; i < count; i++)
    {
        // Server contents mostly undefined
        auto base_server = Server::create_test_server();
        Monitor::Test::add_server(base_server);
    }

    for (int i = 0; i < (int)monitor()->servers().size(); i++)
    {
        auto maria_server = monitor()->servers()[i];
        auto base_server = maria_server->server;
        int id = i + 1;
        if (m_use_hostnames)
        {
            string hostname = create_hostname(id);
            base_server->set_address(hostname);
            base_server->set_port(id);
        }
        else
        {
            maria_server->m_server_id = id;
            monitor()->m_servers_by_id[id] = maria_server;
        }
    }

    m_current_test++;
}

/**
 * Clear dummy servers and free memory
 */
void MariaDBMonitor::Test::clear_servers()
{
    monitor()->m_servers_by_id.clear();
    remove_servers();
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

        MariaDBServer* slave = get_server(slave_id);
        SlaveStatus ss(slave->name());
        ss.slave_io_running = SlaveStatus::SLAVE_IO_YES;
        ss.slave_sql_running = true;
        if (m_use_hostnames)
        {
            ss.settings.master_endpoint = EndPoint(create_hostname(master_id), master_id);
        }
        else
        {
            ss.master_server_id = master_id;
            ss.seen_connected = true;
        }

        slave->m_slave_status.push_back(ss);
    }

    monitor()->build_replication_graph();
    monitor()->find_graph_cycles();
}

/**
 * Check that the nodes have cycles as is expected. Non-cycled nodes must have cycle = 0.
 *
 * @param expected_cycles An array of cycles. Each cycle is an array of server id:s designating the members
 * @return Number of failures
 */
int MariaDBMonitor::Test::check_result_cycles(CycleArray expected_cycles)
{
    string test_name = "Test " + std::to_string(m_current_test) + " ("
        + (m_use_hostnames ? "hostnames" : "server id:s") + "): ";
    int errors = 0;

    // Copy the servers for later comparison.
    std::set<MariaDBServer*> no_cycle_servers(monitor()->servers().begin(), monitor()->servers().end());
    std::set<int> used_cycle_ids;
    for (auto ind_cycles = 0; ind_cycles < MAX_CYCLES; ind_cycles++)
    {
        int cycle_id = NodeData::CYCLE_NONE;
        CycleMembers cycle_member_ids = expected_cycles.cycles[ind_cycles];
        for (auto ind_servers = 0; ind_servers < MAX_CYCLE_SIZE; ind_servers++)
        {
            auto search_id = cycle_member_ids.members[ind_servers];
            if (search_id == 0)
            {
                break;
            }

            MariaDBServer* cycle_server = get_server(search_id);
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
                    cout << test_name << cycle_server->name() << " is in unexpected cycle "
                         << cycle_id << ".\n";
                    errors++;
                }
                else
                {
                    used_cycle_ids.insert(cycle_id);
                }
            }
            else if (cycle_server->m_node.cycle != cycle_id)
            {
                cout << test_name << cycle_server->name() << " is in cycle " << cycle_server->m_node.cycle
                     << " when " << cycle_id << " was expected.\n";
                errors++;
            }
            no_cycle_servers.erase(cycle_server);
        }
    }

    // Check that servers not in expected_cycles are not in a cycle
    for (MariaDBServer* server : no_cycle_servers)
    {
        if (server->m_node.cycle != NodeData::CYCLE_NONE)
        {
            cout << server->name() << " is in cycle " << server->m_node.cycle << " when none was expected.\n";
            errors++;
        }
    }

    return errors;
}

MariaDBServer* MariaDBMonitor::Test::get_server(int id)
{
    auto rval = m_use_hostnames ? monitor()->get_server(EndPoint(create_hostname(id), id)) :
        monitor()->get_server(id);
    mxb_assert(rval);
    return rval;
}

string MariaDBMonitor::Test::create_hostname(int id)
{
    return "hostname" + std::to_string(id) + ".mariadb.com";
}
