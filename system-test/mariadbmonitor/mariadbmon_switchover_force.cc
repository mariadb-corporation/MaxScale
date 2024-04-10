/*
 * Copyright (c) 2023 MariaDB plc
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

#include <maxtest/testconnections.hh>
#include <maxbase/stopwatch.hh>

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    auto normal_status = mxt::ServersInfo::default_repl_states();
    mxs.check_print_servers_status(normal_status);

    if (test.ok())
    {
        // Generate a user with a lock-tables privilege. This is not an admin-user so should not get
        // kicked out during switchover.
        auto admin_conn = mxs.open_rwsplit_connection2_nodb();
        const char lock_user[] = "lock_user";
        const char lock_pw[] = "lock_pw";
        admin_conn->cmd_f("create or replace user %s identified by '%s';", lock_user, lock_pw);
        admin_conn->cmd_f("grant select on test.* to %s;", lock_user);
        admin_conn->cmd_f("grant lock tables on test.* to %s;", lock_user);
        admin_conn->cmd("create table test.t1 (id int);");

        // Log in, take exclusive lock on t1. Log in directly to avoid rwsplit cutting connection to master
        // prematurely.
        test.tprintf("Locking table to prevent normal switchover.");
        auto lock_conn = test.repl->backend(0)->try_open_connection(mxt::MariaDBServer::SslMode::OFF,
                                                                    lock_user, lock_pw, "");
        test.expect(lock_conn->is_open(), "Login as %s failed.", lock_user);
        lock_conn->cmd("lock table test.t1 write;");

        if (test.ok())
        {
            mxb::StopWatch timer;
            test.tprintf("Exclusive lock taken on server1, attempting normal switchover.");
            auto res = mxs.maxctrl("-t 20s call command mariadbmon switchover MariaDB-Monitor");
            // Switch should fail and take switchover_timeout=5s to do so.
            test.expect(res.rc != 0, "Normal switchover succeeded when it should have failed.");
            auto dur_s = mxb::to_secs(timer.lap());
            double dur_s_expected = 4.9;
            test.expect(dur_s > dur_s_expected,
                        "Normal switchover only waited %.1f seconds when %.1f was expected.",
                        dur_s, dur_s_expected);
            // Loses master status for a moment during failed switchover, wait a bit to get it back.
            mxs.wait_for_monitor(1);
            mxs.check_print_servers_status(normal_status);

            if (test.ok())
            {
                // Forced switch should work but still take ~5s.
                test.tprintf("Attempting switchover-force.");
                timer.restart();
                res = mxs.maxctrl("-t 20s call command mariadbmon switchover-force MariaDB-Monitor");
                test.expect(res.rc == 0, "Forced switchover failed: %s", res.output.c_str());
                dur_s = mxb::to_secs(timer.lap());
                test.expect(dur_s > dur_s_expected,
                            "Forced switchover only waited %.1f seconds when %.1f was expected.",
                            dur_s, dur_s_expected);
                mxs.wait_for_monitor(1);
                mxs.check_print_servers_status({slave, master, slave, slave});
            }
        }

        // Disconnect to clear any locks.
        lock_conn = nullptr;
        // Switch back here to handle the case where the first switch unintentionally worked.
        auto res = mxs.maxctrl("call command mariadbmon switchover MariaDB-Monitor server1");
        test.expect(res.rc == 0, "Switchover back to server1 failed.");
        mxs.wait_for_monitor(1);
        mxs.check_print_servers_status(normal_status);

        admin_conn = mxs.open_rwsplit_connection2_nodb();
        admin_conn->cmd("drop table test.t1;");
        admin_conn->cmd_f("drop user %s;", lock_user);

        if (test.ok())
        {
            // MXS-4743
            test.tprintf("Test that switchover-force switches even with a lagging slave.");
            auto& repl = *test.repl;
            auto down = mxt::ServerInfo::DOWN;
            repl.backend(2)->stop_database();
            repl.backend(3)->stop_database();
            mxs.wait_for_monitor();
            mxs.check_print_servers_status({master, slave, down, down});

            if (test.ok())
            {
                auto conn = repl.backend(1)->open_connection();
                auto set_delay = [&conn](int delay) {
                    conn->cmd("stop slave;");
                    conn->cmd_f("change master to master_delay=%i;", delay);
                    conn->cmd("start slave;");
                };

                set_delay(100);
                mxs.wait_for_monitor();
                auto rwsplit_conn = mxs.open_rwsplit_connection2_nodb();
                rwsplit_conn->cmd("flush tables;");
                sleep(1);
                auto servers = mxs.get_servers();
                auto master_gtid = servers.get(0).gtid;
                auto slave_gtid = servers.get(1).gtid;
                test.expect(master_gtid != slave_gtid, "Slave is not lagging as it should.");

                if (test.ok())
                {
                    test.tprintf("Replication delay set.");
                    servers.print();
                    test.tprintf("Trying normal switchover, it should fail.");

                    mxb::StopWatch timer;
                    res = mxs.maxctrl("-t 20s call command mariadbmon switchover MariaDB-Monitor server2");
                    test.expect(res.rc != 0, "Normal switchover succeeded when it should have failed.");
                    auto failtime = mxb::to_secs(timer.lap());
                    double failtime_expected = 1.1;
                    test.expect(failtime < failtime_expected,
                                "Normal switchover only waited %.1f seconds when %.1f or less was expected.",
                                failtime, failtime_expected);

                    mxs.wait_for_monitor(1);
                    mxs.check_print_servers_status({master, slave, down, down});

                    if (test.ok())
                    {
                        test.tprintf("Trying forced switchover, it should succeed.");
                        timer.restart();
                        res = mxs.maxctrl("-t 20s call command mariadbmon switchover-force MariaDB-Monitor "
                                          "server2");
                        test.expect(res.rc == 0, "switchover-force failed: %s", res.output.c_str());
                        auto dur_s = mxb::to_secs(timer.lap());
                        double dur_s_expected = 4.9;
                        test.expect(dur_s > dur_s_expected,
                                    "Normal switchover only waited %.1f seconds when %.1f was expected.",
                                    dur_s, dur_s_expected);
                    }
                }
            }

            repl.backend(2)->start_database();
            repl.backend(3)->start_database();

            // Replication is messed up, reset it.
            mxs.wait_for_monitor(1);
            mxs.maxctrl("call command mariadbmon reset-replication MariaDB-Monitor server1");
            mxs.wait_for_monitor();
            mxs.check_print_servers_status(normal_status);
        }
    }
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
