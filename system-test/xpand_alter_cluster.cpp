/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxtest/maxrest.hh>
#include <maxtest/testconnections.hh>

using namespace std;

class XpandNode
{
public:
    XpandNode(const string& id, const string& ip)
        : m_id(id)
        , m_ip(ip)
    {
    }

    const string& id() const
    {
        return m_id;
    }

    const string& ip() const
    {
        return m_ip;
    }

private:
    string m_id;
    string m_ip;
};

vector<XpandNode> get_nodes(Connection& c)
{
    vector<XpandNode> rv;

    auto result = c.rows("SELECT nodeid, iface_ip FROM system.nodeinfo ORDER BY nodeid");

    for (const auto& row : result)
    {
        rv.push_back(XpandNode(row[0], row[1]));
    }

    return rv;
}

void show_nodes(const vector<XpandNode>& nodes)
{
    for (const auto& node : nodes)
    {
        cout << "Nid: " << node.id() << ", ip: " << node.ip() << endl;
    }
}

bool drop_node(Connection& c, const XpandNode& node)
{
    ostringstream ss;
    ss << "ALTER CLUSTER DROP " << node.id();

    bool rv = c.query(ss.str());

    if (rv)
    {
        cout << "Initiated the dropping of node " << node.id() << "." << endl;

        while (!c.query("SELECT nodeid, iface_ip FROM system.nodeinfo"))
        {
            cout << "Not dropped yet: " << c.error() << endl;
            sleep(1);
        }

        cout << "Dropped node " << node.id() << " at " << node.ip() << "." << endl;
    }
    else
    {
        cout << "Could not drop node: " << c.error() << endl;
    }

    return rv;
}

bool add_node(Connection& c, const string& ip)
{
    ostringstream ss;
    ss << "ALTER CLUSTER ADD '" << ip << "'";

    bool rv = c.query(ss.str());

    if (rv)
    {
        cout << "Initiated the adding of the node at " << ip << "." << endl;

        while (!c.query("SELECT nodeid, iface_ip FROM system.nodeinfo"))
        {
            cout << "Not added yet: " << c.error() << endl;
            sleep(1);
        }

        cout << "Added node " << ip << "." << endl;
    }
    else
    {
        cout << "Could not add node at " << ip << "." << endl;
    }

    return rv;
}

vector<MaxRest::Server> get_volatile_servers(MaxRest& rv)
{
    vector<MaxRest::Server> volatiles;
    vector<MaxRest::Server> all = rv.list_servers();

    for (const auto& server : all)
    {
        if (server.name.find("@@") == 0)
        {
            volatiles.push_back(server);
        }
    }

    return volatiles;
}

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);

    TestConnections test(argc, argv);

    // Ensure remnants of earlier tests are not used.
    test.maxscale->ssh_output("rm -f /var/lib/maxscale/Xpand-Monitor/xpand_nodes-v1.db");

    Connection c = test.xpand->get_connection(0);

    if (c.connect())
    {
        vector<XpandNode> direct_nodes = get_nodes(c);
        int nDirect = direct_nodes.size();

        show_nodes(direct_nodes);

        // Remove one node from the Cluster before MaxScale starts.
        drop_node(c, direct_nodes.back());
        --nDirect;

        show_nodes(get_nodes(c));

        test.maxscale->start();

        test.maxscale->wait_for_monitor(2);

        MaxRest mr(&test, test.maxscale);

        auto volatile_servers = get_volatile_servers(mr);

        int nVia_maxscale = volatile_servers.size();

        cout << "MaxScale sees:" << endl;
        for (const auto& server: volatile_servers)
        {
            cout << server.name << endl;
        }

        test.expect(nVia_maxscale == nDirect, "MaxScale sees %d servers, %d expected", nVia_maxscale, nDirect);

        // Add the node back.
        add_node(c, direct_nodes.back().ip());
        ++nDirect;

        show_nodes(get_nodes(c));

        // In the config we have 'cluster_monitor_interval=5s' so it should take
        // at most 5 attempts before the node is detected. But let's be generous
        int nMax_attempts = 10;
        int nAttempts = 0;

        do
        {
            ++nAttempts;

            auto servers = get_volatile_servers(mr);

            cout << endl;
            for (const auto& server: servers)
            {
                cout << server.name << endl;
            }

            nVia_maxscale = servers.size();

            if (nVia_maxscale != nDirect)
            {
                cout << "Still only " << nVia_maxscale << " and not " << nDirect << "." << endl;
                sleep(1);
            }
        }
        while (nVia_maxscale != nDirect && nAttempts < nMax_attempts);

        test.expect(nVia_maxscale == nDirect, "MaxScale sees %d servers, %d expected", nVia_maxscale, nDirect);
    }
    else
    {
        test.add_failure("Could not connect to xpand: %s", c.error());
    }

    return test.global_result;
}
