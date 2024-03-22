/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <iostream>
#include "mariadbmon_utils.hh"

using std::string;
using std::cout;
using std::endl;

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    auto maxconn = mxs.open_rwsplit_connection2("test");
    // Advance gtid:s a bit to so gtid variables are updated.
    generate_traffic_and_check(test, maxconn.get(), 10);

    auto status = mxs.get_servers();
    status.check_servers_status(mxt::ServersInfo::default_repl_states());
    status.print();
    string gtid_begin = status.get(0).gtid;

    // Leave first of three slaves connected so it's clear which one is the master server.
    const char STOP_SLAVE[] = "STOP SLAVE;";
    const char RESET_SLAVE[] = "RESET SLAVE ALL;";
    const char READ_ONLY_OFF[] = "SET GLOBAL read_only=0;";
    const int FIRST_MOD_NODE = 2;   // Modify nodes 2 & 3
    const int NODE_COUNT = 4;

    for (int i = FIRST_MOD_NODE; i < NODE_COUNT; i++)
    {
        auto conn = repl.backend(i)->open_connection();
        if (!(conn->cmd(STOP_SLAVE) && conn->cmd(RESET_SLAVE) && conn->cmd(READ_ONLY_OFF)))
        {
            test.add_failure("Could not stop slave connections and/or disable read_only for node %d.", i);
        }
    }

    const int diverging_server = 3;
    // Add more events to node3.
    test.tprintf("Sending more inserts to server 4.");
    auto conn = repl.backend(diverging_server)->open_connection();
    generate_traffic_and_check(test, conn.get(), 10);
    // Save gtids
    status = mxs.get_servers();
    string gtid_node2 = status.get(2).gtid;
    string gtid_node3 = status.get(diverging_server).gtid;
    status.print();
    test.expect(gtid_begin == gtid_node2, "Node2 unexpected gtid: %s", gtid_node2.c_str());
    test.expect(gtid_node2 < gtid_node3, "Node3 gtid did not advance: %s", gtid_node3.c_str());

    const string REJOIN_CMD = "call command mariadbmon rejoin MariaDB-Monitor";
    string rejoin_s3 = REJOIN_CMD + " server3";
    string rejoin_s4 = REJOIN_CMD + " server4";

    if (test.ok())
    {
        cout << "Sending rejoin commands for servers 3 & 4. Server 4 should not rejoin the cluster.\n";

        mxs.maxctrl(rejoin_s3);
        mxs.maxctrl(rejoin_s4);
        mxs.wait_for_monitor(2);

        mxs.check_print_servers_status({mxt::ServerInfo::master_st, mxt::ServerInfo::slave_st,
                                        mxt::ServerInfo::slave_st, mxt::ServerInfo::RUNNING});
    }

    // Finally, fix replication by telling the current master to replicate from server4
    test.tprintf("Setting server 1 to replicate from server 4. Manually rejoin servers 2 and 3.");
    const char CHANGE_CMD_FMT[] = "CHANGE MASTER TO MASTER_HOST = '%s', MASTER_PORT = %d, "
                                  "MASTER_USE_GTID = current_pos, MASTER_USER='repl', "
                                  "MASTER_PASSWORD = 'repl';";

    conn = repl.backend(0)->open_connection();
    conn->cmd_f(CHANGE_CMD_FMT, repl.ip_private(diverging_server), repl.port(diverging_server));
    conn->cmd("START SLAVE;");
    mxs.wait_for_monitor(2);
    string rejoin_s2 = REJOIN_CMD + " server2";
    mxs.maxctrl(rejoin_s2);
    mxs.maxctrl(rejoin_s3);
    mxs.wait_for_monitor(2);

    mxs.check_print_servers_status({mxt::ServerInfo::slave_st, mxt::ServerInfo::slave_st,
                                    mxt::ServerInfo::slave_st, mxt::ServerInfo::master_st});
    mxs.maxctrl("call command mysqlmon switchover MariaDB-Monitor server1");
    mxs.wait_for_monitor(2);
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
}
