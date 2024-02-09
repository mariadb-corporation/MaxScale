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

/**
 * MXS-1493: https://jira.mariadb.org/browse/MXS-1493
 *
 * Testing of master failure verification
 */

#include <maxtest/testconnections.hh>

namespace
{
void test_main(TestConnections& test)
{
    auto& repl = *test.repl;
    auto& mxs = *test.maxscale;
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        // Send a simple event, wait for sync, block master.
        auto conn = mxs.open_rwsplit_connection2();
        conn->cmd("flush tables;");
        test.sync_repl_slaves();
        mxs.wait_for_monitor();
        mxs.get_servers().print();

        auto down = mxt::ServerInfo::DOWN;
        auto master = mxt::ServerInfo::master_st;
        auto slave = mxt::ServerInfo::slave_st;

        test.tprintf("Blocking master and checking that master failure is delayed at least once.");
        repl.block_node(0);
        mxs.sleep_and_wait_for_monitor(1, 1);
        mxs.check_print_servers_status({down, slave, slave, slave});
        // Monitor first sees that master is down.
        test.log_includes("If primary does not return in .* monitor tick(s), failover begins.");
        // Once failcount loops have passed, monitor checks slaves for most recent event.
        mxs.wait_for_monitor(2);
        test.log_includes("Delaying failover for at least");

        test.tprintf("Waiting to see if failover is performed.");
        sleep(10);
        mxs.check_print_servers_status({down, master, slave, slave});
        repl.unblock_node(0);

        mxs.wait_for_monitor();
        if (test.ok())
        {
            test.tprintf("Rejoining server1 and switching back.");
            mxs.maxctrl("call command mariadbmon rejoin MySQL-Monitor server1");
            mxs.wait_for_monitor(2);
            mxs.check_print_servers_status({slave, master, slave, slave});
            if (test.ok())
            {
                mxs.maxctrl("call command mariadbmon switchover MySQL-Monitor");
                mxs.wait_for_monitor(2);
                mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
            }
        }
    }
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
