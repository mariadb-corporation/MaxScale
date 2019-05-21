/**
 * @file clustrix_mon.cpp - simple Clustrix monitor test
 * Just creates Clustrix cluster and connect Maxscale to it
 * It can be used as a template for clustrix tests
 *
 * See Clustrix_nodes.h for details about configiration
 */

#include <iostream>
#include <maxbase/string.hh>
#include <maxscale/jansson.hh>
#include "testconnections.h"
#include "maxrest.hh"

using namespace std;

namespace
{

const set<string> bootstrap_servers =
{
    "clustrix_server1",
    "clustrix_server2",
    "clustrix_server3",
    "clustrix_server4",
};

const std::string monitor_name = "Clustrix-Monitor";


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
            test.expect(false, "The name of a dynamic Clustrix node does not start with \"%s\": %s",
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

void run_test(TestConnections& test)
{
    MaxRest maxrest(&test);

    check_for_servers(maxrest);
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
