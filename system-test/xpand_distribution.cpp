/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <map>
#include <vector>
#include <maxtest/maxrest.hh>
#include <maxtest/testconnections.hh>

using namespace std;

namespace
{

const int N = 60;

Connection create_rcr_connection(TestConnections& test)
{
    return test.maxscale->readconn_master();
}

Connection create_rws_connection(TestConnections& test)
{
    return test.maxscale->rwsplit();
}

void run_test(TestConnections& test,
              const std::string& router,
              Connection (*create_connection)(TestConnections& test))
{
    vector<Connection> connections;

    cout << "Creating " << N << " connections: " << flush;

    for (int i = 1; i <= N; ++i)
    {
        cout << i << " " << flush;
        Connection c = create_connection(test);
        test.expect(c.connect(), "Could not connect to %s.", router.c_str());

        connections.emplace_back(std::move(c));
    }

    cout << endl;

    MaxRest maxrest(&test);

    auto servers = maxrest.list_servers();

    map<string, int> connections_by_server;

    for (auto server : servers)
    {
        if (server.name.front() == '@') // A dynamic server
        {
            connections_by_server.insert(std::make_pair(server.name, server.connections));
        }
    }

    int n = N / connections_by_server.size();
    int lb = (n * 90) / 100;
    int ub = (n * 110) / 100 + 1;

    for (auto kv : connections_by_server)
    {
        bool acceptable = (kv.second >= lb && kv.second <= ub);

        cout << kv.first << ": " << kv.second << " connections, which is ";
        if (!acceptable)
        {
            cout << "NOT ";
        }
        cout << "within the accepted range [" << lb << ", " << ub << "]." << endl;

        test.expect(acceptable,
                    "%s has %d connections, accepted range: [%d, %d].",
                    kv.first.c_str(), kv.second, lb, ub);
    }
}

}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    cout << "\nTesting RCR" << endl;
    run_test(test, "RCR", create_rcr_connection);

    cout << "\nTesting RWS" << endl;
    run_test(test, "RWS", create_rws_connection);

    return test.global_result;
}
