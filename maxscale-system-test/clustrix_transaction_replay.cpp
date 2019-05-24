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

        if (!node_by_address.count(server.address) == 0)
        {
            Clustrix_nodes* pClustrix = test.clustrix;

            for (auto i = 0; i < pClustrix->N; ++i)
            {
                if (pClustrix->IP_private[i] == server.address)
                {
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

void setup(TestConnections& test, MYSQL* pMysql)
{
    drop_table(test, pMysql);
    create_table(test, pMysql);
}

void run_test(TestConnections& test)
{
    MaxRest maxrest(&test);

    collect_information(test);

    Maxscales* pMaxscales = test.maxscales;
    test.add_result(pMaxscales->connect_rwsplit(), "Could not connect to RWS.");

    MYSQL* pMysql = pMaxscales->conn_rwsplit[0];

    setup(test, pMysql);

    // What node are we connected to?
    Row row = get_row(pMysql, "SELECT iface_ip FROM system.nodeinfo WHERE nodeid=gtmnid()");

    test.expect(row.size() == 1, "1 row expected, %d received.", (int)row.size());

    string ip = row[0];
    string static_name = static_by_address[ip].name;
    string dynamic_name = dynamic_by_address[ip].name;
    int node = node_by_address[ip];

    cout << "Connected to " << ip << ", which is "
         << static_name << " and " << dynamic_name
         << " running on node " << node << "."
         << endl;

    test.try_query(pMysql, "BEGIN");
    test.try_query(pMysql, "SELECT * FROM test.clustrix_tr");

    Clustrix_nodes* pClustrix = test.clustrix;

    auto rv = pClustrix->ssh_output("service clustrix stop", node, true);
    test.expect(rv.first == 0, "Could not stop Clustrix on node %d.", node);

    MaxRest::Server server;

    do
    {
        server = maxrest.show_server(dynamic_name);

        if (server.state.find("Down") == string::npos)
        {
            sleep(1);
        }
    }
    while (server.state.find("Down") == string::npos);

    cout << "Clustrix on node " << node << " is down." << endl;

    // The server we were connected to is now down. If the following
    // succeeds, then reconnect + transaction replay worked as specified.
    test.try_query(pMysql, "SELECT * FROM test.clustrix_tr");
    test.try_query(pMysql, "COMMIT");

    cout << "Starting Clustrix on node " << node << "." << endl;
    rv = pClustrix->ssh_output("service clustrix start", node, true);
    test.expect(rv.first == 0, "Could not start Clustrix on node %d.", node);

    time_t start = time(nullptr);
    time_t end;
    long max_wait = 3 * 60;

    do
    {
        server = maxrest.show_server(dynamic_name);

        if (server.state.find("Down") != string::npos)
        {
            cout << "Still down..." << endl;
            sleep(1);
        }

        end = time(nullptr);
    }
    while ((server.state.find("Down") != string::npos) && (end - start < max_wait));

    test.expect(end - start < max_wait, "Clustrix node %d did not start.", node);
}

}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    try
    {
        run_test(test);
    }
    catch (const std::exception& x)
    {
        cout << "Exception: " << x.what() << endl;
    }

    return test.global_result;
}
