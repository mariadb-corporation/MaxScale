/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// MXS-2652: https://jira.mariadb.org/browse/MXS-2652

#include <maxtest/testconnections.hh>
#include "mariadbmon_utils.hh"

using std::string;

namespace
{
void expect_maintenance(TestConnections& test, int node, bool expected)
{
    auto server_info = test.maxscale->get_servers().get(node);
    bool in_maint = server_info.status & mxt::ServerInfo::MAINT;
    test.expect(in_maint == expected, "Wrong maintenance status on node %i. Got %i, expected %i.",
                node, in_maint, expected);
}

void expect_running(TestConnections& test, int node, bool expected)
{
    auto server_info = test.maxscale->get_servers().get(node);
    bool running = server_info.status & mxt::ServerInfo::RUNNING;
    test.expect(running == expected, "Wrong running status on node %i. Got %i, expected %i.",
                node, running, expected);
}
}

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
    auto conn = mxs.open_rwsplit_connection2();
    generate_traffic_and_check(test, conn.get(), 5);

    if (test.ok())
    {
        // Make all three slaves ineligible for promotion in different ways.
        int stop_slave_ind = 1;
        int binlog_ind = 2;
        int maint_ind = 3;

        repl.ping_or_open_admin_connections();
        // Slave 1. Just stop slave.
        repl.backend(stop_slave_ind)->admin_connection()->cmd("STOP SLAVE;");
        // Slave 2. Disable binlog.

        repl.stop_node(binlog_ind);
        repl.stash_server_settings(binlog_ind);
        repl.disable_server_setting(binlog_ind, "log-bin");
        repl.start_node(binlog_ind);
        mxs.wait_for_monitor(2);

        // Slave 3. Set node to maintenance, then restart it. Check issue
        // MXS-2652: Maintenance flag should persist when server goes down & comes back up.

        string maint_srv_name = "server" + std::to_string(maint_ind + 1);
        expect_maintenance(test, maint_ind, false);

        if (test.ok())
        {
            mxs.maxctrl("set server " + maint_srv_name + " maintenance");
            mxs.wait_for_monitor();
            expect_running(test, maint_ind, true);
            expect_maintenance(test, maint_ind, true);

            repl.stop_node(maint_ind);
            mxs.wait_for_monitor();
            expect_running(test, maint_ind, false);
            expect_maintenance(test, maint_ind, true);

            repl.start_node(maint_ind);
            mxs.wait_for_monitor();
            expect_running(test, maint_ind, true);
            expect_maintenance(test, maint_ind, true);

            if (test.ok())
            {
                auto maint_running = mxt::ServerInfo::RUNNING | mxt::ServerInfo::MAINT;
                mxs.check_print_servers_status({mxt::ServerInfo::master_st, mxt::ServerInfo::RUNNING,
                                                mxt::ServerInfo::slave_st,  maint_running});
                test.tprintf("Blocking master. Failover should not happen.");

                repl.block_node(0);
                mxs.sleep_and_wait_for_monitor(2, 2);
                mxs.check_print_servers_status({mxt::ServerInfo::DOWN, mxt::ServerInfo::RUNNING,
                                                mxt::ServerInfo::slave_st,  maint_running});
                repl.unblock_node(0);
            }

            // Remove maintenance.
            mxs.maxctrl("clear server " + maint_srv_name + " maintenance");
        }

        // Restore normal settings.
        repl.stop_node(binlog_ind);
        repl.restore_server_settings(binlog_ind);
        repl.start_node(binlog_ind);

        repl.backend(stop_slave_ind)->admin_connection()->cmd("START SLAVE;");
        mxs.wait_for_monitor();
        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    }
}
