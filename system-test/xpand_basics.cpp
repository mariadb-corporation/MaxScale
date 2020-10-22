/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#include <iostream>
#include <maxbase/string.hh>
#include <maxscale/jansson.hh>
#include <maxtest/maxrest.hh>
#include <maxtest/testconnections.hh>

using namespace std;

namespace
{

const set<string> bootstrap_servers =
{
    "xpand_server1",
    "xpand_server2",
    "xpand_server3",
    "xpand_server4",
};

const std::string monitor_name = "Xpand-Monitor";

void expect_all_servers_to_be(const MaxRest& maxrest, const std::string& state)
{
    cout << "Expecting the state of all servers to be: " << state << endl;

    TestConnections& test = maxrest.test();
    auto servers = maxrest.list_servers();

    for (const auto& server : servers)
    {
        cout << server.name << "(" << server.address << "): " << server.state << endl;
        test.expect(server.state.find(state) != string::npos,
                    "State of %s(%s) is '%s', expected '%s.",
                    server.name.c_str(),
                    server.address.c_str(),
                    server.state.c_str(),
                    state.c_str());
    }
}

void expect_server_to_be(const MaxRest& maxrest, const MaxRest::Server& server, const std::string& state)
{
    TestConnections& test = maxrest.test();
    cout << "Expecting the state of '" << server.name << "' to be '" << state << "'." << endl;

    test.expect(server.state.find(state) != string::npos,
                "State of '%s' was not '%s', but '%s'.",
                server.name.c_str(),
                state.c_str(),
                server.state.c_str());
}

void check_for_servers(const MaxRest& maxrest)
{
    TestConnections& test = maxrest.test();

    auto servers = maxrest.list_servers();

    test.expect(servers.size() >= bootstrap_servers.size(),
                "Expected at least %d servers.", (int)bootstrap_servers.size());

    set<string> static_servers;
    set<string> dynamic_servers;

    string prefix = "@@" + monitor_name;

    for (const auto& server : servers)
    {
        string name = server.name;

        cout << "Looking at: " << name << endl;

        if (bootstrap_servers.find(name) != bootstrap_servers.end())
        {
            static_servers.insert(name);
            continue;
        }

        if (name.find(prefix) != 0)
        {
            test.expect(false, "The name of a dynamic Xpand node does not start with \"%s\": %s",
                        prefix.c_str(), name.c_str());
        }

        dynamic_servers.insert(name);
    }

    test.expect(static_servers == bootstrap_servers,
                "Did not find expected servers.\n"
                "Found   : %s\n"
                "Expected: %s",
                mxb::join(static_servers).c_str(),
                mxb::join(bootstrap_servers).c_str());

    test.expect(dynamic_servers.size() == 4,
                "Did not find expected numbers of servers %d != 4: %s",
                (int)dynamic_servers.size(),
                mxb::join(dynamic_servers).c_str());
}

void check_state_change(const MaxRest& maxrest)
{
    TestConnections& test = maxrest.test();

    expect_all_servers_to_be(maxrest, "Master, Running");
    cout << endl;

    int node = 0;
    string address = test.xpand->IP_private[node];

    cout << "Blocking node: " << node << endl;
    test.xpand->block_node(node);

    int cycles = 3;
    cout << "Waiting for " << cycles << " monitor cycles." << endl;
    test.maxscales->wait_for_monitor(cycles);

    auto servers = maxrest.list_servers();

    for (const auto& server : servers)
    {
        cout << server.name << "(" << server.address << "): " << server.state << endl;
        if (server.address == address)
        {
            test.expect(server.state == "Down",
                        "Blocked server was not 'Down' but '%s'.", server.state.c_str());
        }
    }

    cout << endl;

    test.xpand->unblock_node(node);
    cout << "Waiting for " << cycles << " monitor cycles." << endl;
    test.maxscales->wait_for_monitor(cycles);

    expect_all_servers_to_be(maxrest, "Master, Running");
    cout << endl;
}

void check_softfailing(const MaxRest& maxrest)
{
    TestConnections& test = maxrest.test();

    string id("@@Xpand-Monitor:node-2"); // Just an arbitrary dynamic node.

    MaxRest::Server before = maxrest.show_server(id);
    expect_server_to_be(maxrest, before, "Master, Running");

    cout << "Softfailing " << id << "." << endl;
    maxrest.call_command("xpandmon", "softfail", monitor_name, { "@@Xpand-Monitor:node-2" });

    MaxRest::Server during = maxrest.show_server(id);
    expect_server_to_be(maxrest, during, "Drained");

    cout << "Unsoftfailing " << id << "." << endl;
    maxrest.call_command("xpandrixmon", "unsoftfail", monitor_name, { "@@Xpand-Monitor:node-2" });

    MaxRest::Server after = maxrest.show_server(id);
    expect_server_to_be(maxrest, after, "Master, Running");
}

void run_test(TestConnections& test)
{
    MaxRest maxrest(&test);

    check_for_servers(maxrest);
    check_state_change(maxrest);
    check_softfailing(maxrest);
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
