/*
 * Copyright (c) 2024 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include "mariadbmon_utils.hh"

using std::string;

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    auto running = mxt::ServerInfo::RUNNING;
    auto down = mxt::ServerInfo::DOWN;

    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        auto maxconn = mxs.open_rwsplit_connection2_nodb();
        generate_traffic_and_check(test, maxconn.get(), 10);
        repl.sync_slaves();

        test.tprintf("All servers synced, safe failover should happen.");
        int master_ind = 0;
        repl.stop_node(master_ind);
        mxs.wait_for_monitor(5);
        mxs.check_print_servers_status({down, master, slave, slave});
        repl.start_node(master_ind);
        mxs.sleep_and_wait_for_monitor(1, 2);
        mxs.check_print_servers_status({slave, master, slave, slave});

        if (test.ok())
        {
            master_ind = 1;
            auto run_on_slaves = [&repl, master_ind](const string& cmd) {
                for (int i = 0; i < repl.N; i++)
                {
                    if (i != master_ind)
                    {
                        repl.backend(i)->admin_connection()->cmd(cmd);
                    }
                }
            };
            test.tprintf("Stop slaves, add events only to master, then shutdown master.");
            run_on_slaves("stop slave");
            mxs.wait_for_monitor();
            mxs.check_print_servers_status({running, master, running, running});
            auto conn = repl.backend(master_ind)->open_connection();
            generate_traffic_and_check_nosync(test, conn.get(), 10);
            mxs.wait_for_monitor();
            mxs.get_servers().print();
            repl.stop_node(master_ind);
            mxs.wait_for_monitor();
            mxs.get_servers().print();

            test.tprintf("Resume replication from shutdown master and wait for failover. "
                         "It should not happen as it would lose events.");
            run_on_slaves("start slave");

            mxs.wait_for_monitor();
            mxs.check_print_servers_status({slave, down, slave, slave});
            mxs.wait_for_monitor(5);
            mxs.check_print_servers_status({slave, down, slave, slave});

            const char safe_failover[] = "call command mariadbmon failover-safe MariaDB-Monitor";

            if (test.ok())
            {
                test.tprintf("Try manual failover-safe.");
                auto res = mxs.maxctrl(safe_failover);
                test.expect(res.rc != 0, "Safe failover succeeded when it should have failed.");
                test.tprintf("Command output: %s", res.output.c_str());
                const char expected_msg[] = "relay log is missing transactions";
                test.expect(res.output.find(expected_msg) != string::npos, "Did not find expected message.");
            }

            repl.start_node(master_ind);
            mxs.wait_for_monitor();
            mxs.check_print_servers_status({slave, master, slave, slave});
            test.tprintf("Stop and start replication again to ensure reconnection.");
            run_on_slaves("stop slave");
            run_on_slaves("start slave");
            mxs.wait_for_monitor();
            mxs.check_print_servers_status({slave, master, slave, slave});

            if (test.ok())
            {
                test.tprintf("Finally, turn off auto-failover and check that manual failover-safe works "
                             "when all data is replicated.");
                mxs.maxctrl("alter monitor MariaDB-Monitor auto_failover=off");
                repl.stop_node(master_ind);
                mxs.wait_for_monitor();
                auto res = mxs.maxctrl(safe_failover);
                test.expect(res.rc == 0, "Safe failover failed: %s", res.output.c_str());
                mxs.wait_for_monitor();
                mxs.check_print_servers_status({master, down, slave, slave});
                repl.start_node(master_ind);
                mxs.wait_for_monitor(2);
                mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
            }
        }
    }
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
