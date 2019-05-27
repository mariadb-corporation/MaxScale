/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <iostream>
#include <map>
#include "testconnections.h"
#include "maxrest.hh"

using namespace std;

namespace
{

const std::string monitor_name = "Clustrix-Monitor";

map<string, MaxRest::Server> static_by_address;
map<string, MaxRest::Server> dynamic_by_address;
map<string, int> node_by_address;

void collect_information(TestConnections& test)
{
    MaxRest maxrest(&test);

    auto servers = maxrest.list_servers();

    string prefix = "@@" + monitor_name;

    for (const auto& server : servers)
    {
        string name = server.name;

        if (name.find("@@") == 0)
        {
            dynamic_by_address.insert(make_pair(server.address, server));
        }
        else
        {
            static_by_address.insert(make_pair(server.address, server));
        }

        if (node_by_address.count(server.address) == 0)
        {
            Clustrix_nodes* pClustrix = test.clustrix;

            for (auto i = 0; i < pClustrix->N; ++i)
            {
                if (pClustrix->IP_private[i] == server.address)
                {
                    cout << server.address << " IS NODE " << i << endl;
                    node_by_address[server.address] = i;
                    break;
                }
            }
        }
    }
}

void drop_table(TestConnections& test, MYSQL* pMysql)
{
    test.try_query(pMysql, "DROP TABLE IF EXISTS test.clustrix_tr");
}

void create_table(TestConnections& test, MYSQL* pMysql)
{
    test.try_query(pMysql, "CREATE TABLE test.clustrix_tr (a INT)");
    test.try_query(pMysql, "INSERT INTO test.clustrix_tr VALUES (42)");
}

void setup_database(TestConnections& test)
{
    MYSQL* pMysql = test.maxscales->open_rwsplit_connection();
    test.expect(pMysql, "Could not open connection to rws.");

    drop_table(test, pMysql);
    create_table(test, pMysql);

    mysql_close(pMysql);
}

bool stop_server(TestConnections& test, const std::string& name, int node)
{
    bool stopped = false;

    Clustrix_nodes* pClustrix = test.clustrix;

    auto rv = pClustrix->ssh_output("service clustrix stop", node, true);
    test.expect(rv.first == 0, "Could not stop Clustrix on node %d.", node);

    if (rv.first == 0)
    {
        MaxRest maxrest(&test);
        MaxRest::Server server;

        do
        {
            server = maxrest.show_server(name);

            if (server.state.find("Down") == string::npos)
            {
                sleep(1);
            }
        }
        while (server.state.find("Down") == string::npos);

        cout << "Clustrix on node " << node << " is down." << endl;
        stopped = true;
    }

    return stopped;
}

bool start_server(TestConnections& test, const std::string& name, int node, int timeout)
{
    bool started = false;

    Clustrix_nodes* pClustrix = test.clustrix;

    auto rv = pClustrix->ssh_output("service clustrix start", node, true);
    test.expect(rv.first == 0, "Could not start Clustrix on node %d.", node);

    if (rv.first == 0)
    {
        MaxRest maxrest(&test);
        MaxRest::Server server;

        time_t start = time(nullptr);
        time_t end;

        do
        {
            server = maxrest.show_server(name);

            if (server.state.find("Down") != string::npos)
            {
                cout << "Still down..." << endl;
                sleep(1);
            }

            end = time(nullptr);
        }
        while ((server.state.find("Down") != string::npos) && (end - start < timeout));

        test.expect(end - start < timeout, "Clustrix node %d did not start.", node);

        if (server.state.find("Master") != string::npos)
        {
            started = true;
        }
    }

    return started;
}

MaxRest::Server get_current_server(TestConnections& test, MYSQL* pMysql)
{
    Row row = get_row(pMysql, "SELECT iface_ip FROM system.nodeinfo WHERE nodeid=gtmnid()");

    test.expect(row.size() == 1, "1 row expected, %d received.", (int)row.size());

    string address = row[0];

    return dynamic_by_address[address];
}

void test_transaction_replay(TestConnections& test, MYSQL* pMysql, const std::string& name, int node)
{
    test.try_query(pMysql, "BEGIN");
    test.try_query(pMysql, "SELECT * FROM test.clustrix_tr");

    cout << "Stopping server " << name << " on node " << node << "." << endl;
    if (stop_server(test, name, node))
    {
        // The server we were connected to is now down. If the following
        // succeeds, then reconnect + transaction replay worked as specified.
        test.try_query(pMysql, "SELECT * FROM test.clustrix_tr");
        test.try_query(pMysql, "COMMIT");

        cout << "Starting Clustrix " << name << " on node " << node << "." << endl;

        int timeout = 3 * 60;
        start_server(test, name, node, timeout);
    }
}

void run_test(TestConnections& test)
{
    MaxRest maxrest(&test);

    Maxscales* pMaxscales = test.maxscales;
    test.add_result(pMaxscales->connect_rwsplit(), "Could not connect to RWS.");

    MYSQL* pMysql = pMaxscales->conn_rwsplit[0];

    MaxRest::Server server = get_current_server(test, pMysql);

    string dynamic_name = server.name;
    string static_name = static_by_address[server.address].name;
    int node = node_by_address[server.address];

    cout << "Connected to " << server.address << ", which is "
         << dynamic_name << "(" << static_name << ") "
         << " running on node " << node << "."
         << endl;

    // FIRST TEST: Take down the very node we are connected to.
    //
    // This requires MaxScale to open a new connection to another node,
    // seed the session and replay the transaction.
    test_transaction_replay(test, pMysql, dynamic_name, node);

    MaxRest::Server server2 = get_current_server(test, pMysql);

    test.expect(server.address != server2.address, "Huh, server did not switch.");

    server = server2;

    for (const auto& kv : dynamic_by_address)
    {
        if (kv.first != server.address)
        {
            server2 = kv.second;
            break;
        }
    }

    node = node_by_address[server2.address];

    // SECOND TEST: Take down another node but than the one we are connected to.
    //              That will cause a  Clustrix Group Change event.
    //
    // This requires MaxScale to detect the error and replay the transaction.
    test_transaction_replay(test, pMysql, server2.name, node);

    server2 = get_current_server(test, pMysql);

    test.expect(server.address == server2.address, "Huh, server has switched.");
}

}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    try
    {
        collect_information(test);
        setup_database(test);

        run_test(test);
    }
    catch (const std::exception& x)
    {
        cout << "Exception: " << x.what() << endl;
    }

    return test.global_result;
}
