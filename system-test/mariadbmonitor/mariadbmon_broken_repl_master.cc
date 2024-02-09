/*
 * Copyright (c) 2023 MariaDB plc
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

// MXS-4798, MXS-4841

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    auto running = mxt::ServerInfo::RUNNING;
    auto down = mxt::ServerInfo::DOWN;

    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    test.tprintf("Stop replication from server3 & 4.");
    for (int i = 2; i < repl.N; i++)
    {
        auto conn = repl.backend(i)->admin_connection();
        conn->cmd("stop slave;");
        conn->cmd("reset slave all;");
    }
    mxs.wait_for_monitor(1);
    mxs.check_print_servers_status({master, slave, running, running});

    if (test.ok())
    {
        test.tprintf("Stop sql thread from server2, then stop master. Wait and check that master doesn't "
                     "change.");
        auto broken_be = repl.backend(1);
        auto conn = broken_be->admin_connection();
        conn->cmd("stop slave sql_thread;");
        mxs.wait_for_monitor(1);
        auto one_master = {master, running, running, running};
        mxs.check_print_servers_status(one_master);

        auto old_master = repl.backend(0);
        old_master->stop_database();
        mxs.sleep_and_wait_for_monitor(3, 3);   // Sleep for longer than failcount.
        mxs.check_print_servers_status({down, running, running, running});

        test.tprintf("Start old master, it should regain [Master].");
        old_master->start_database();
        mxs.sleep_and_wait_for_monitor(1, 1);
        mxs.check_print_servers_status(one_master);

        test.tprintf("Stop all but server2 and restart MaxScale. Check that server2 does not get promoted.");
        for (int i = 0; i < repl.N; i++)
        {
            if (i != 1)
            {
                repl.backend(i)->stop_database();
            }
        }

        mxs.sleep_and_wait_for_monitor(1, 1);
        mxs.restart();
        mxs.sleep_and_wait_for_monitor(3, 3);   // Sleep for longer than failcount.
        mxs.check_print_servers_status({down, running, down, down});

        if (test.ok())
        {
            test.tprintf("Start server4, it should not become master.");
            repl.backend(3)->start_database();
            mxs.sleep_and_wait_for_monitor(1, 1);
            mxs.check_print_servers_status({down, running, down, running});

            test.tprintf("Totally stop replication on server2, it should become master.");
            broken_be->admin_connection()->cmd("stop slave;");
            mxs.sleep_and_wait_for_monitor(1, 1);
            mxs.check_print_servers_status({down, master, down, running});

            test.tprintf("Redirect server2->server4, server4 should become master.");
            repl.replicate_from(1, 3);
            mxs.sleep_and_wait_for_monitor(1, 1);
            mxs.check_print_servers_status({down, slave, down, master});

            test.tprintf("Start server1 and 3, master should stick.");
            old_master->start_database();
            repl.backend(2)->start_database();
            mxs.sleep_and_wait_for_monitor(1, 1);
            mxs.check_print_servers_status({running, slave, running, master});
        }
    }

    // Cleanup.
    for (int i = 0; i < repl.N; i++)
    {
        repl.backend(i)->start_database();
        if (i != 0)
        {
            repl.replicate_from(i, 0);
        }
    }
}
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
