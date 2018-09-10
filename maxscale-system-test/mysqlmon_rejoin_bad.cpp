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

#include "testconnections.h"
#include "fail_switch_rejoin_common.cpp"

using std::string;

int main(int argc, char** argv)
{
    char result_tmp[bufsize];
    interactive = strcmp(argv[argc - 1], "interactive") == 0;
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);
    MYSQL* maxconn = test.maxscales->open_rwsplit_connection(0);

    // Set up test table
    basic_test(test);
    // Delete binlogs to sync gtid:s
    delete_slave_binlogs(test);
    // Advance gtid:s a bit to so gtid variables are updated.
    generate_traffic_and_check(test, maxconn, 10);
    test.repl->sync_slaves(0);

    test.tprintf(LINE);
    print_gtids(test);
    test.tprintf(LINE);
    string gtid_begin;
    if (find_field(maxconn, GTID_QUERY, GTID_FIELD, result_tmp) == 0)
    {
        gtid_begin = result_tmp;
    }
    mysql_close(maxconn);
    test.tprintf("Stopping MaxScale...");
    // Mess with the slaves to fix situation such that only one slave can be rejoined. Stop maxscale.
    if (test.stop_maxscale(0))
    {
        test.expect(false, "Could not stop MaxScale.");
        return test.global_result;
    }

    // Leave first of three slaves connected so it's clear which one is the master server.
    const char STOP_SLAVE[] = "STOP SLAVE;";
    const char RESET_SLAVE[] = "RESET SLAVE ALL;";
    const char READ_ONLY_OFF[] = "SET GLOBAL read_only=0;";
    test.repl->connect();
    const int FIRST_MOD_NODE = 2; // Modify nodes 2 & 3
    const int NODE_COUNT = test.repl->N;
    MYSQL** nodes = test.repl->nodes;

    for (int i = FIRST_MOD_NODE; i < NODE_COUNT; i++)
    {
        if (mysql_query(nodes[i], STOP_SLAVE) != 0 ||
                mysql_query(nodes[i], RESET_SLAVE) != 0 ||
                mysql_query(nodes[i], READ_ONLY_OFF) != 0)
        {
            test.expect(false, "Could not stop slave connections and/or disable read_only for node %d.", i);
            return test.global_result;
        }
    }

    // Add more events to node3.
    string gtid_node2, gtid_node3;
    test.tprintf("Sending more inserts to server 4.");
    generate_traffic_and_check(test, nodes[3], 10);
    // Save gtids
    if (find_field(nodes[2], GTID_QUERY, GTID_FIELD, result_tmp) == 0)
    {
        gtid_node2 = result_tmp;
    }
    if (find_field(nodes[3], GTID_QUERY, GTID_FIELD, result_tmp) == 0)
    {
        gtid_node3 = result_tmp;
    }
    print_gtids(test);
    bool gtids_ok = (gtid_begin == gtid_node2 && gtid_node2 < gtid_node3);
    test.expect(gtids_ok, "Gtid:s have not advanced correctly.");
    if (!gtids_ok)
    {
        return test.global_result;
    }
    test.tprintf("Restarting MaxScale. Server 4 should not rejoin the cluster.");
    test.tprintf(LINE);
    if (test.start_maxscale(0))
    {
        test.expect(false, "Could not start MaxScale.");
        return test.global_result;
    }
    test.maxscales->wait_for_monitor();
    get_output(test);

    StringSet node2_states = test.get_server_status("server3");
    StringSet node3_states = test.get_server_status("server4");
    bool states_n2_ok = (node2_states.find("Slave") != node2_states.end());
    bool states_n3_ok = (node3_states.find("Slave") == node3_states.end());
    test.expect(states_n2_ok, "Node 2 has not rejoined when it should have.");
    test.expect(states_n3_ok, "Node 3 rejoined when it shouldn't have.");
    if (!states_n2_ok || !states_n3_ok)
    {
        return test.global_result;
    }
    // Finally, fix replication by telling the current master to replicate from server4
    test.tprintf("Setting server 1 to replicate from server 4. Auto-rejoin should redirect servers 2 and 3.");
    const char CHANGE_CMD_FMT[] = "CHANGE MASTER TO MASTER_HOST = '%s', MASTER_PORT = %d, "
                                  "MASTER_USE_GTID = current_pos, MASTER_USER='repl', MASTER_PASSWORD = 'repl';";
    char cmd[256];
    snprintf(cmd, sizeof(cmd), CHANGE_CMD_FMT, test.repl->IP[3], test.repl->port[3]);
    mysql_query(nodes[0], cmd);
    mysql_query(nodes[0], "START SLAVE;");
    test.maxscales->wait_for_monitor();
    get_output(test);
    int master_id = get_master_server_id(test);
    test.expect(master_id == 4, "Server 4 should be the cluster master.");
    StringSet node0_states = test.get_server_status("server1");
    bool states_n0_ok = (node0_states.find("Slave") != node0_states.end() &&
                         node0_states.find("Relay Master") == node0_states.end());
    test.expect(states_n0_ok, "Server 1 is not a slave when it should be.");
    if (states_n0_ok)
    {
        int ec;
        test.maxscales->ssh_node_output(0,
                                        "maxadmin call command mysqlmon switchover MySQL-Monitor server1 server4" , true, &ec);
        test.maxscales->wait_for_monitor();
        master_id = get_master_server_id(test);
        test.expect(master_id == 1, "Server 1 should be the cluster master.");
        get_output(test);
    }

    test.repl->fix_replication();
    return test.global_result;
}
