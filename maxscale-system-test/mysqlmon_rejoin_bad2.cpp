/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <vector>
#include <iostream>
#include <iterator>

#include "testconnections.h"
#include "fail_switch_rejoin_common.cpp"

using std::string;
using std::cout;
using std::endl;

static void expect(TestConnections& test, const char* zServer, const StringSet& expected)
{
    StringSet found = test.get_server_status(zServer);

    std::ostream_iterator<string> oi(cout, ", ");

    cout << zServer
         << ", expected states: ";
    std::copy(expected.begin(), expected.end(), oi);
    cout << endl;

    cout << zServer
         << ", found states   : ";
    std::copy(found.begin(), found.end(), oi);
    cout << endl;

    if (found != expected)
    {
        cout << "ERROR, found states are not the same as the expected ones." << endl;
        ++test.global_result;
    }

    cout << endl;
}

static void expect(TestConnections& test, const char* zServer, const char* zState)
{
    StringSet s;
    s.insert(zState);

    expect(test, zServer, s);
}

static void expect(TestConnections& test, const char* zServer, const char* zState1, const char* zState2)
{
    StringSet s;
    s.insert(zState1);
    s.insert(zState2);

    expect(test, zServer, s);
}

int main(int argc, char** argv)
{
    interactive = strcmp(argv[argc - 1], "interactive") == 0;
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);
    MYSQL* maxconn = test.maxscales->open_rwsplit_connection(0);

    // Set up test table
    basic_test(test);
    // Delete binlogs to sync gtid:s
    delete_slave_binlogs(test);
    // Advance gtid:s a bit to so gtid variables are updated.
    generate_traffic_and_check(test, maxconn, 5);
    test.repl->sync_slaves(0);
    get_output(test);

    print_gtids(test);
    get_input();
    mysql_close(maxconn);

    // Stop master, wait for failover
    cout << "Stopping master, should auto-failover." << endl;
    int master_id_old = get_master_server_id(test);
    test.repl->stop_node(0);
    sleep(5);
    get_output(test);
    int master_id_new = get_master_server_id(test);
    cout << "Master server id is " << master_id_new << endl;
    test.assert(master_id_new > 0 && master_id_new != master_id_old,
        "Failover did not promote a new master.");
    if (test.global_result != 0)
    {
        return test.global_result;
    }

    // Stop maxscale to prevent an unintended rejoin.
    if (test.stop_maxscale(0))
    {
        test.assert(false, "Could not stop MaxScale.");
        return test.global_result;
    }
    // Restart old master. Then add some events to it.
    test.repl->start_node(0, (char*)"");
    sleep(3);
    test.repl->connect();
    cout << "Adding more events to node 0. It should not join the cluster." << endl;
    generate_traffic_and_check(test, test.repl->nodes[0], 5);
    print_gtids(test);
    // Restart maxscale. Should not rejoin old master.
    if (test.start_maxscale(0))
    {
        test.assert(false, "Could not start MaxScale.");
        return test.global_result;
    }
    sleep(5);
    get_output(test);

    expect(test, "server1", "Running");
    if (test.global_result != 0)
    {
        cout << "Old master is a member or the cluster when it should not be." << endl;
        return test.global_result;
    }

    // Set current master to replicate from the old master. The old master should remain as the current master.
    cout << "Setting server " << master_id_new << " to replicate from server 1. Server " << master_id_new
         << " should remain as the master because server 1 doesn't have the latest event it has." << endl;
    const char CHANGE_CMD_FMT[] = "CHANGE MASTER TO MASTER_HOST = '%s', MASTER_PORT = %d, "
        "MASTER_USE_GTID = current_pos, MASTER_USER='repl', MASTER_PASSWORD = 'repl';";
    char cmd[256];
    int ind = master_id_new - 1;
    snprintf(cmd, sizeof(cmd), CHANGE_CMD_FMT, test.repl->IP[0], test.repl->port[0]);
    MYSQL** nodes = test.repl->nodes;
    mysql_query(nodes[ind], cmd);
    mysql_query(nodes[ind], "START SLAVE;");
    sleep(5);
    get_output(test);

    expect(test, "server1", "Running");
    expect(test, "server2", "Master", "Running");
    expect(test, "server3", "Slave", "Running");
    expect(test, "server4", "Slave", "Running");
    test.repl->fix_replication();
    return test.global_result;
}
