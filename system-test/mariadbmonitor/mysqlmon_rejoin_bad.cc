/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include "mariadbmon_utils.hh"

using std::string;

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

    repl.ping_or_open_admin_connections();
    // Advance gtid:s a bit to so gtid variables are updated.
    auto maxconn = mxs.open_rwsplit_connection2("test");
    generate_traffic_and_check(test, maxconn.get(), 1);

    auto status = mxs.get_servers();
    status.check_servers_status(mxt::ServersInfo::default_repl_states());
    status.print();
    string gtid_begin = status.get(0).gtid;

    test.tprintf("Stopping MaxScale...");
    // Mess with the slaves to fix situation such that only one slave can be rejoined. Stop maxscale.
    mxs.stop_and_check_stopped();

    if (test.ok())
    {
        // Leave first of three slaves connected so it's clear which one is the master server.
        const char STOP_SLAVE[] = "STOP SLAVE;";
        const char RESET_SLAVE[] = "RESET SLAVE ALL;";
        const char READ_ONLY_OFF[] = "SET GLOBAL read_only=0;";

        const int FIRST_MOD_NODE = 2;   // Modify nodes 2 & 3
        const int NODE_COUNT = 4;
        for (int i = FIRST_MOD_NODE; i < NODE_COUNT; i++)
        {
            auto conn = repl.backend(i)->open_connection();
            if (!conn->cmd(STOP_SLAVE) || !conn->cmd(RESET_SLAVE) || !conn->cmd(READ_ONLY_OFF))
            {
                test.add_failure("Could not stop slave connections and/or disable read_only for node %d.", i);
            }
        }

        if (test.ok())
        {
            // Add more events to node3.
            test.tprintf("Sending more inserts to server 4.");
            auto conn = repl.backend(3)->open_connection();
            generate_traffic_and_check_nosync(test, conn.get(), 10);

            // Save gtids
            string query = "SELECT @@gtid_current_pos;";
            string gtid_node2 = repl.backend(2)->admin_connection()->simple_query(query);
            string gtid_node3 = repl.backend(3)->admin_connection()->simple_query(query);

            test.expect(gtid_begin == gtid_node2, "Unexpected gtid: %s", gtid_node2.c_str());
            test.expect(gtid_node2 < gtid_node3, "Gtid:s have not advanced correctly.");

            test.tprintf("Restarting MaxScale. Server 4 should not rejoin the cluster.");
            if (mxs.start_and_check_started())
            {
                mxs.wait_for_monitor(2);
                mxs.check_print_servers_status({mxt::ServerInfo::master_st, mxt::ServerInfo::slave_st,
                                                mxt::ServerInfo::slave_st, mxt::ServerInfo::RUNNING});
            }
        }

        // Finally, fix replication by telling the current master to replicate from server4
        test.tprintf("Setting server 1 to replicate from server 4. Auto-rejoin should redirect servers 2 "
                     "and 3.");
        const char CHANGE_CMD_FMT[] = "CHANGE MASTER TO MASTER_HOST = '%s', MASTER_PORT = %d, "
                                      "MASTER_USE_GTID = current_pos, MASTER_USER='repl', "
                                      "MASTER_PASSWORD = 'repl';";
        auto conn = repl.backend(0)->admin_connection();
        conn->cmd_f(CHANGE_CMD_FMT, repl.ip_private(3), repl.port(3));
        conn->cmd_f("START SLAVE;");
        mxs.wait_for_monitor(2);
        mxs.check_print_servers_status({mxt::ServerInfo::slave_st, mxt::ServerInfo::slave_st,
                                        mxt::ServerInfo::slave_st, mxt::ServerInfo::master_st});

        test.tprintf("Reseting cluster...");
        mxs.maxctrl("call command mariadbmon reset-replication MariaDB-Monitor server1");
        mxs.wait_for_monitor(1);
        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    }
}
