/*
 * Copyright (c) 2024 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

using std::string;

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

    // First test classical servers_no_promotion behavior. It should stop autoselection of servers during
    // failover/switchover.

    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    auto* srv1 = repl.backend(0);
    test.tprintf("Stopping master, server4 should be promoted.");
    srv1->stop_database();
    mxs.wait_for_monitor(4);
    mxs.check_print_servers_status({down, slave, slave, master});

    test.tprintf("Try to run switchover, it should fail as autoselecting new master is impossible.");
    const string switch_cmd = "call command mariadbmon switchover MariaDB-Monitor";
    auto res = mxs.maxctrl(switch_cmd);
    if (res.rc == 0)
    {
        test.add_failure("Switchover succeeded when it should have failed.");
    }
    else
    {
        test.tprintf("%s", res.output.c_str());
    }

    test.tprintf("Starting server1, switchover should now work.");
    srv1->start_database();
    mxs.sleep_and_wait_for_monitor(1, 2);
    mxs.check_print_servers_status({slave, slave, slave, master});
    res = mxs.maxctrl(switch_cmd);
    test.expect(res.rc == 0, "Switchover failed: %s", res.output.c_str());
    mxs.wait_for_monitor(1);
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    test.tprintf("Manually switchover to server2, it should bypass servers_no_promotion.");
    res = mxs.maxctrl("call command mariadbmon switchover MariaDB-Monitor server2");
    test.expect(res.rc == 0, "Switchover failed: %s", res.output.c_str());
    mxs.wait_for_monitor(2);
    mxs.check_print_servers_status({slave, master, slave, slave});

    if (test.ok())
    {
        test.tprintf("Shutdown server1 and set server2 to read_only. server2 should change to [Slave] "
                     "but no master is elected as no server is valid to take over.");
        repl.execute_query_all_nodes("set global read_only=0;");
        srv1->stop_database();
        auto srv2 = repl.backend(1);
        srv2->admin_connection()->cmd_f("set global read_only=1;");
        mxs.sleep_and_wait_for_monitor(1, 1);
        mxs.check_print_servers_status({down, slave, slave, slave});

        test.tprintf("Disable auto_rejoin to stop monitor from interfering.");
        mxs.alter_monitor("MariaDB-Monitor", "auto_rejoin", "false");
        mxs.sleep_and_wait_for_monitor(1, 1);

        test.tprintf("Stop slave on server3. It should not gain [Master] due to exclusion.");
        const string stop_slave = "stop slave;";
        auto srv3 = repl.backend(2);
        srv3->admin_connection()->cmd(stop_slave);
        mxs.wait_for_monitor(1);
        mxs.check_print_servers_status({down, slave, running, slave});

        test.tprintf("Stop slave on server4. It should gain [Master].");
        auto srv4 = repl.backend(3);
        srv4->admin_connection()->cmd(stop_slave);
        mxs.wait_for_monitor(2);
        mxs.check_print_servers_status({down, running, running, master});

        test.tprintf("Restoring...");
        srv1->start_database();
        mxs.sleep_and_wait_for_monitor(1, 1);
        repl.replicate_from(0, 3);
        repl.replicate_from(1, 3);
        repl.replicate_from(2, 3);
        mxs.check_print_servers_status({slave, slave, slave, master});

        res = mxs.maxctrl(switch_cmd);
        test.expect(res.rc == 0, "Switchover failed: %s", res.output.c_str());
        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    }
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
