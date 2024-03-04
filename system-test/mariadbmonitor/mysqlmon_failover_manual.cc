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
#include "mariadbmon_utils.hh"

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

    const std::string failover = "call command mariadbmon failover MariaDB-Monitor";
    const std::string switchover = "call command mariadbmon switchover MariaDB-Monitor server1";

    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        test.tprintf("Part 1: Stop master and run manual failover.");
        repl.stop_node(0);
        mxs.wait_for_monitor(1);
        mxs.maxctrl(failover);
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
        test.tprintf("Part 2: Disable replication on server1 and stop master. Run manual async-failover and "
                     "check that server3 is promoted.");
        int stop_ind = 0;
        int old_master_ind = 1;
        auto conn = repl.backend(stop_ind)->admin_connection();
        conn->cmd("STOP SLAVE;");
        conn->cmd("RESET SLAVE ALL;");

        repl.stop_node(old_master_ind);
        mxs.wait_for_monitor(1);

        // Instead of normal manual failover, check that async-failover works.
        mxs.maxctrl("call command mariadbmon async-failover MariaDB-Monitor");
        mxs.wait_for_monitor(2);
        auto res = mxs.maxctrl("call command mariadbmon fetch-cmd-result MariaDB-Monitor");
        if (res.rc == 0)
        {
            // The output is a json string. Check that it includes the success-message.
            auto found = (res.output.find("failover completed successfully") != std::string::npos);
            test.expect(found, "Result json did not contain expected message. Result: %s",
                        res.output.c_str());
            mxs.check_print_servers_status({running, down, master, slave});
            auto maxconn = mxs.open_rwsplit_connection2();
            generate_traffic_and_check(test, maxconn.get(), 5);
        }
        else
        {
            test.add_failure("fetch-cmd-result failed: %s", res.output.c_str());
        }

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
        mxs.maxctrl(failover);
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
}
