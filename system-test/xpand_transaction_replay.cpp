/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <map>
#include <maxtest/maxrest.hh>
#include <maxtest/testconnections.hh>

using namespace std;

namespace
{

const std::string monitor_name = "Xpand-Monitor";

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
            Xpand_nodes* pXpand = test.xpand;

            for (auto i = 0; i < pXpand->N; ++i)
            {
                if (pXpand->IP_private[i] == server.address)
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
    test.try_query(pMysql, "DROP TABLE IF EXISTS test.xpand_tr");
}

void create_table(TestConnections& test, MYSQL* pMysql)
{
    test.try_query(pMysql, "CREATE TABLE test.xpand_tr (a INT)");
    test.try_query(pMysql, "INSERT INTO test.xpand_tr VALUES (42)");
}

void setup_database(TestConnections& test)
{
    MYSQL* pMysql = test.maxscales->open_rwsplit_connection();
    test.expect(pMysql, "Could not open connection to rws.");

    drop_table(test, pMysql);
    create_table(test, pMysql);

    mysql_close(pMysql);
}

bool wait_for_state(TestConnections& test, const std::string& name, int timeout, const std::string& state)
{
    MaxRest maxrest(&test);
    MaxRest::Server server;

    time_t start = time(nullptr);
    time_t end;

    do
    {
        server = maxrest.show_server(name);

        if (server.state.find(state) == string::npos)
        {
            cout << name << " still not " << state << "..." << endl;
            sleep(1);
        }

        end = time(nullptr);
    }
    while ((server.state.find(state) == string::npos) && (end - start < timeout));

    test.expect(server.state.find(state) != string::npos,
                "Xpand node %s did not change state to %s within timeout of %d.",
                name.c_str(), state.c_str(), timeout);

    return server.state.find(state) != string::npos;
}

bool stop_server(TestConnections& test, const std::string& name, int node, int timeout)
{
    bool stopped = false;

    Xpand_nodes* pXpand = test.xpand;

    auto rv = pXpand->ssh_output("service clustrix stop", node, true);
    test.expect(rv.first == 0, "Could not stop Xpand on node %d.", node);

    if (rv.first == 0)
    {
        if (wait_for_state(test, name, timeout, "Down"))
        {
            cout << "Xpand on node " << node << " is down." << endl;
            stopped = true;
        }
    }

    return stopped;
}

bool start_server(TestConnections& test, const std::string& name, int node, int timeout)
{
    bool started = false;

    Xpand_nodes* pXpand = test.xpand;

    auto rv = pXpand->ssh_output("service clustrix start", node, true);
    test.expect(rv.first == 0, "Could not start Xpand on node %d.", node);

    if (rv.first == 0)
    {
        if (wait_for_state(test, name, timeout, "Master"))
        {
            cout << "Xpand on node " << node << " is up." << endl;
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
    cout << "Beginning transaction..." << endl;
    test.try_query(pMysql, "BEGIN");
    test.try_query(pMysql, "SELECT * FROM test.xpand_tr");

    cout << "Stopping server " << name << "(node " << node << ")." << endl;
    int timeout;
    timeout = 60; // Going down should be fast...
    if (stop_server(test, name, node, timeout))
    {
        // The server we were connected to is now down. If the following
        // succeeds, then reconnect + transaction replay worked as specified.
        cout << "Continuing transaction..." << endl;
        test.try_query(pMysql, "SELECT * FROM test.xpand_tr");
        test.try_query(pMysql, "COMMIT");

        cout << "Bring Xpand " << name << "(node " << node << ") up again." << endl;

        timeout = 3 * 60; // Coming up takes time...
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
         << "running on node " << node << "."
         << endl;

    // FIRST TEST: Take down the very node we are connected to.
    //
    // This requires MaxScale to open a new connection to another node,
    // seed the session and replay the transaction.
    cout << "\nTESTING transaction replay when connected to node goes down." << endl;
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
    //              That will cause a  Xpand Group Change event.
    //
    // This requires MaxScale to detect the error and replay the transaction.
    cout << "\nTESTING transaction replay when group change error occurs." << endl;
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
