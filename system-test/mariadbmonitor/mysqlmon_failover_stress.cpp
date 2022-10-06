/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-10-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
#include <random>
#include <maxtest/testconnections.hh>
#include "fail_switch_rejoin_common.cpp"
#include "mariadbmon_utils.hh"

using namespace std;

// The test now runs only two failovers. Change for a longer time limit later.
// TODO: add semisync to remove this limitation.

// Number of backends
const int N = 4;

namespace
{

void list_servers(TestConnections& test)
{
    test.print_maxctrl("list servers");
}

bool check_server_status(TestConnections& test, int id)
{
    bool is_master = false;

    MariaDBCluster* pRepl = test.repl;

    string server = string("server") + std::to_string(id);

    StringSet statuses = test.get_server_status(server.c_str());
    std::ostream_iterator<string> oi(cout, " ");

    cout << server << ": ";
    std::copy(statuses.begin(), statuses.end(), oi);

    cout << " => ";

    if (statuses.count("Master"))
    {
        is_master = true;
        cout << "OK";
    }
    else if (statuses.count("Slave"))
    {
        cout << "OK";
    }
    else if (statuses.count("Running"))
    {
        MYSQL* pConn = pRepl->nodes[id - 1];

        char result[1024];
        if (find_field(pConn, "SHOW SLAVE STATUS", "Last_IO_Error", result) == 0)
        {
            const char needle[] =
                ", which is not in the master's binlog. "
                "Since the master's binlog contains GTIDs with higher sequence numbers, "
                "it probably means that the slave has diverged due to executing extra "
                "erroneous transactions";

            if (strstr(result, needle))
            {
                // A rejoin was attempted, but it failed because the node (old master)
                // had events that were not present in the new master. That is, a rejoin
                // is not possible in principle without corrective action.
                cout << "OK (could not be joined due to GTID issue)";
            }
            else
            {
                cout << result;
                test.expect(false, "Merely 'Running' node did not error in expected way.");
            }
        }
        else
        {
            test.expect(false, "Could not execute \"SHOW SLAVE STATUS\"");
        }
    }
    else
    {
        test.expect(false, "Unexpected server state for %s.", server.c_str());
    }

    cout << endl;

    return is_master;
}

void check_server_statuses(TestConnections& test)
{
    int masters = 0;

    masters += check_server_status(test, 1);
    masters += check_server_status(test, 2);
    masters += check_server_status(test, 3);
    masters += check_server_status(test, 4);

    if (masters == 0)
    {
        test.global_result = 0;
        test.tprintf("No master, checking that autofail has been turned off.");
        test.log_includes("disabling automatic failover");
    }
    else if (masters != 1)
    {
        test.expect(!true, "Unexpected number of masters: %d", masters);
    }
}

bool is_valid_server_id(TestConnections& test, int id)
{
    std::set<int> ids;
    test.repl->connect();

    for (int i = 0; i < N; i++)
    {
        ids.insert(test.repl->get_server_id(i));
    }

    test.repl->disconnect();
    return ids.count(id);
}

void run(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    testclient::Settings sett;
    sett.host = mxs.ip4();
    sett.port = mxs.rwsplit_port;
    sett.user = mxs.user_name();
    sett.pw = mxs.password();
    sett.rows = 100;

    testclient::ClientGroup clients(test, 4, sett);
    clients.prepare();

    if (test.ok())
    {
        clients.start();

        for (int i = 0; i < 2; i++)
        {
            mxs.wait_for_monitor();

            int master_id = get_master_server_id(test);

            if (is_valid_server_id(test, master_id))
            {
                test.reset_timeout();
                cout << "\nStopping node: " << master_id << endl;
                test.repl->stop_node(master_id - 1);

                test.maxscale->wait_for_monitor();
                list_servers(test);

                test.maxscale->wait_for_monitor();
                list_servers(test);

                test.reset_timeout();
                test.maxscale->wait_for_monitor();
                cout << "\nStarting node: " << master_id << endl;
                test.repl->start_node(master_id - 1);

                test.maxscale->wait_for_monitor();
                list_servers(test);

                test.maxscale->wait_for_monitor();
                list_servers(test);
            }
            else
            {
                test.expect(false, "Unexpected master id: %d", master_id);
            }
        }

        mxs.wait_for_monitor();
        clients.stop();

        test.repl->close_connections();
        test.repl->connect();

        check_server_statuses(test);
    }
    clients.cleanup();
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, run);
}
