/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-09-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include "mariadbmon_utils.hh"

// This test is effectively "mysqlmon_failover_manual" with automatic failover.
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

    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    auto down = mxt::ServerInfo::DOWN;
    auto running = mxt::ServerInfo::RUNNING;

    const std::string switchover = "call command mariadbmon switchover MariaDB-Monitor server1";

    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        test.tprintf("Part 1: Stop master and wait for failover.");
        repl.stop_node(0);
        mxs.wait_for_monitor(2);
        mxs.check_print_servers_status({down, master, slave, slave});
        auto maxconn = mxs.open_rwsplit_connection2();
        generate_traffic_and_check(test, maxconn.get(), 5);
        repl.start_node(0);
        repl.replicate_from(0, 1);
        mxs.wait_for_monitor(1);
        mxs.check_print_servers_status({slave, master, slave, slave});
    }

    if (test.ok())
    {
        test.tprintf("Part 2: Disable replication on server1 and stop master. Check that server3 is "
                     "promoted.");
        int stop_ind = 0;
        int old_master_ind = 1;
        auto conn = repl.backend(stop_ind)->admin_connection();
        conn->cmd("STOP SLAVE;");
        conn->cmd("RESET SLAVE ALL;");

        repl.stop_node(old_master_ind);
        mxs.wait_for_monitor(2);

        mxs.check_print_servers_status({running, down, master, slave});
        auto maxconn = mxs.open_rwsplit_connection2();
        generate_traffic_and_check(test, maxconn.get(), 5);

        repl.start_node(old_master_ind);

        repl.replicate_from(stop_ind, 2);
        repl.replicate_from(old_master_ind, 2);
        mxs.wait_for_monitor(1);
        mxs.check_print_servers_status({slave, slave, master, slave});
        mxs.maxctrl(switchover);
        mxs.wait_for_monitor(1);
        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    }

    if (test.ok())
    {
        test.tprintf("Part 3: Disable log_bin on server2, making it invalid for promotion. Disable "
                     "log-slave-updates on server3. Check that server4 is promoted on master failure.");
        prepare_log_bin_failover_test(test);

        int old_master_ind = 0;
        repl.stop_node(old_master_ind);
        mxs.wait_for_monitor(2);
        mxs.check_print_servers_status({down, slave, slave, master});

        auto maxconn = mxs.open_rwsplit_connection2();
        generate_traffic_and_check(test, maxconn.get(), 5);
        repl.start_node(old_master_ind);

        cleanup_log_bin_failover_test(test);
        mxs.check_print_servers_status({running, slave, slave, master});
        repl.replicate_from(old_master_ind, 3);
        mxs.wait_for_monitor(1);
        mxs.maxctrl(switchover);
        mxs.wait_for_monitor(1);
        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    }

    if (test.ok())
    {
        // MXS-5688
        const char check_repl_setting[] = "check_repl_on_stop_slave_timeout";
        const char unlock_tables[] = "unlock tables";

        mxs.delete_log();
        test.tprintf("Enable %s. Then, lock all replicas and advance gtid on "
                     "server1.", check_repl_setting);
        mxs.alter_monitor("MariaDB-Monitor", check_repl_setting, "true");
        mxs.wait_for_monitor();

        repl.ping_or_open_admin_connections();
        for (int i = 1; i < repl.N; i++)
        {
            repl.backend(i)->admin_connection()->cmd("flush tables with read lock;");
        }
        int old_master_ind = 0;
        // The event quickly replicates to slaves, they just cannot process it yet.
        repl.backend(old_master_ind)->admin_connection()->cmd("flush tables;");
        mxs.wait_for_monitor();

        auto servers = mxs.get_servers();
        const auto master_gtid = servers.get(old_master_ind).gtid;

        bool gtids_ok = false;
        for (int i = 0; i < 3 && !gtids_ok && test.ok(); i++)
        {
            bool all_gtid_io_pos_ok = true;
            servers = mxs.get_servers();

            for (int j = 1; j < repl.N; j++)
            {
                auto& replica_info = servers.get(j);
                test.expect(replica_info.gtid != master_gtid, "server %i reached master gtid even with "
                                                              "ftwrl on.", j + 1);
                if (replica_info.slave_connections.empty()
                    || replica_info.slave_connections[0].gtid != master_gtid)
                {
                    all_gtid_io_pos_ok = false;
                }
            }

            if (all_gtid_io_pos_ok)
            {
                gtids_ok = true;
            }
            else
            {
                sleep(1);
            }
        }

        auto release_locks = [&repl, &unlock_tables]() {
            for (int i = 1; i < repl.N; i++)
            {
                repl.backend(i)->admin_connection()->cmd(unlock_tables);
            }
        };

        if (gtids_ok)
        {
            test.tprintf("Stop master. Failover should stall until at least one lock is released.");
            repl.backend(old_master_ind)->stop_database();
            mxs.wait_for_monitor(3);
            mxs.log_matches("To avoid data loss, failover is postponed");
            mxs.check_print_servers_status({down, slave, slave, slave});
            test.tprintf("Release lock on server2, failover should begin. Redirecting servers 3 & 4 should "
                         "stall.");
            repl.backend(1)->admin_connection()->cmd(unlock_tables);
            sleep(2);
            mxs.log_matches("Performing automatic failover");
            test.tprintf("Sleep more, \"STOP SLAVE\" should time out.");
            sleep(5);
            mxs.log_matches("According to .+, replication from .+ is still active.");
            test.tprintf("Release locks, failover should complete.");
            release_locks();
            mxs.wait_for_monitor(2);
            test.tprintf("Restart old master and rejoin it.");
            repl.backend(old_master_ind)->start_database();
            mxs.wait_for_monitor();
            mxs.maxctrlf("call command mariadbmon rejoin MariaDB-Monitor server1");
            mxs.check_print_servers_status({slave, master, slave, slave});
            mxs.maxctrlf("call command mariadbmon switchover MariaDB-Monitor server1");
            mxs.wait_for_monitor();
            mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
        }
        else
        {
            test.add_failure("Gtid:s did not reach expected values.");
        }

        release_locks();
        mxs.alter_monitor("MariaDB-Monitor", check_repl_setting, "false");
    }
}
