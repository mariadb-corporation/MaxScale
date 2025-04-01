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
#include <string>

using std::string;

namespace
{
const auto normal_status = mxt::ServersInfo::default_repl_states();
auto master = mxt::ServerInfo::master_st;
auto slave = mxt::ServerInfo::slave_st;

void test_connector_timeout(TestConnections& test);
void test_missing_privs(TestConnections& test);

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;

    mxs.wait_for_monitor();
    mxs.check_servers_status(normal_status);

    test.tprintf("Create table and insert some data.");
    auto conn = mxs.open_rwsplit_connection2("test");
    conn->cmd("CREATE OR REPLACE TABLE test.t1(id INT)");

    conn->cmd("BEGIN");
    for (int i = 0; i < 10; ++i)
    {
        conn->cmd_f("INSERT INTO test.t1 VALUES (%i)", i);
    }
    conn->cmd("COMMIT");

    test.tprintf("Trying to do manual switchover to server2");
    test.maxctrl("call command mysqlmon switchover MySQL-Monitor server2 server1");

    mxs.wait_for_monitor();
    mxs.check_servers_status({slave, master, slave, slave});

    if (test.ok())
    {
        test.tprintf("Switchover success. Resetting situation using async-switchover.");
        test.maxctrl("call command mariadbmon async-switchover MySQL-Monitor server1");
        // Wait a bit so switch completes, then fetch results.
        mxs.wait_for_monitor(2);
        auto res = test.maxctrl("call command mariadbmon fetch-cmd-result MySQL-Monitor");
        test.expect(res.rc == 0, "fetch-cmd-result failed: %s", res.output.c_str());
        if (test.ok())
        {
            // The output is a json string. Check that it includes "switchover completed successfully".
            auto found = (res.output.find("switchover completed successfully") != string::npos);
            test.expect(found, "Result json did not contain expected message. Result: %s",
                        res.output.c_str());
        }
        mxs.check_servers_status(normal_status);
    }

    if (test.ok())
    {
        test_connector_timeout(test);
    }

    if (test.ok())
    {
        test_missing_privs(test);
    }
}

void test_connector_timeout(TestConnections& test)
{
    test.tprintf("Lock a table on master, and start switchover.");
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    const string lock_cmd = "lock table test.t1 write;";
    const string unlock_cmd = "unlock tables;";
    const string async_switchover = "call command mariadbmon async-switchover MySQL-Monitor";
    const string fetch_results = "call command mariadbmon fetch-cmd-result MySQL-Monitor";
    const char so_not_started[] = "Switchover did not start: %s";

    mxs.delete_log();

    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    auto master_conn = repl.backend(0)->open_connection();
    master_conn->cmd(lock_cmd);
    auto res = test.maxctrl(async_switchover);
    test.expect(res.rc == 0, so_not_started, res.output.c_str());

    if (test.ok())
    {
        auto so_running = [&mxs, &fetch_results](){
            auto fetch_res = mxs.maxctrl(fetch_results);
            return fetch_res.output.find("running") != string::npos;
        };

        const char should_be_running[] = "Switchover should still be running.";
        const char log_pattern[] = "FOR SET GLOBAL read_only=1;. timed out on .* Retrying with";
        const char unexpected_timeout_msg[] = "Log should not include timeout message.";

        test.tprintf("Check that switchover is still running and that log has no timeout message.");

        // The timings here are restrictive enough that any change to the related monitor code or monitor
        // settings can cause a fail, but is required to get valid results.

        // Timeout should not happen in at least 5 seconds.
        test.tprintf("Sleep 4 seconds, then check log.");
        sleep(4);
        test.expect(so_running(), should_be_running);
        test.expect(!mxs.log_matches(log_pattern), unexpected_timeout_msg);

        test.tprintf("Sleep some more, connector should time out. Switchover itself should still be "
                     "running.");
        sleep(5);
        test.expect(mxs.log_matches(log_pattern), "No expected timeout message.");
        test.expect(so_running(), should_be_running);
        test.tprintf("Sleep again, switchover should time out.");
        sleep(7);
        test.expect(!so_running(), "Switchover should have timed out.");

        res = mxs.maxctrl(fetch_results);
        test.expect(res.output.find("failed") != string::npos, "Switchover should have failed.");

        if (test.ok())
        {
            test.tprintf("Table lock is still held. Start async switchover again. Check that it completes "
                         "once lock is released.");
            mxs.delete_log();
            res = test.maxctrl(async_switchover);
            test.expect(res.rc == 0, so_not_started, res.output.c_str());

            test.tprintf("Sleep 4 seconds, then check log.");
            sleep(4);
            test.expect(so_running(), should_be_running);
            test.expect(!mxs.log_matches(log_pattern), unexpected_timeout_msg);

            test.tprintf("Release lock, wait for switchover to complete.");
            master_conn->cmd(unlock_cmd);
            sleep(2);
            test.expect(!so_running(), "Switchover should no longer be running.");
            res = mxs.maxctrl(fetch_results);
            test.expect(res.output.find("successfully") != string::npos, "Switchover should have succeeded.");

            res = mxs.maxctrl("call command mariadbmon switchover MySQL-Monitor server1");
            test.expect(res.rc == 0, "Switchover to standard config failed: %s", res.output.c_str());
        }
    }
    master_conn->cmd(unlock_cmd);
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
}

void test_missing_privs(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    test.tprintf("MXS-4605: Monitor should reconnect if command fails due to missing privileges.");
    mxs.stop();
    auto* master_srv = repl.backend(0);
    auto conn = master_srv->open_connection();
    conn->cmd_f("grant slave monitor on *.* to mariadbmon;");
    conn->cmd_f("revoke super, read_only admin on *.* from mariadbmon;");
    repl.sync_slaves();
    // Close connections so monitor does not attempt to kill them.
    conn = nullptr;
    repl.close_connections();
    repl.close_admin_connections();

    mxs.start();

    mxs.check_servers_status(normal_status);
    if (test.ok())
    {
        auto try_switchover = [&](const string& expected_errmsg,
                                  mxt::ServerInfo::bitfield expected_server2_state) {
            const string switch_cmd = "call command mysqlmon switchover MySQL-Monitor server2";
            auto res = test.maxctrl(switch_cmd);
            if (expected_errmsg.empty())
            {
                if (res.rc == 0)
                {
                    test.tprintf("Switchover succeeded.");
                }
                else
                {
                    test.add_failure("Switchover failed. Error: %s", res.output.c_str());
                }
            }
            else
            {
                if (res.rc == 0)
                {
                    test.add_failure("Switchover succeeded when it should have failed.");
                }
                else
                {
                    test.tprintf("Switchover failed as expected. Error: %s", res.output.c_str());
                    test.expect(res.output.find(expected_errmsg) != string::npos,
                                "Did not find expected error message.");
                    mxs.check_print_servers_status({master, expected_server2_state, slave, slave});
                }
            }
            mxs.wait_for_monitor();
        };

        test.tprintf("Trying switchover, it should fail due to missing privs.");
        try_switchover("Failed to enable read_only on", slave);

        if (test.ok())
        {
            conn = master_srv->open_connection();
            conn->cmd_f("grant super, read_only admin on *.* to mariadbmon;");
            conn = nullptr;

            repl.sync_slaves();
            repl.close_admin_connections();

            test.tprintf("Privileges granted. Switchover should still fail, as monitor connections are "
                         "using the grants of their creation time.");
            // In 23.08 and later, the monitor makes a new connection to master when starting switchover.
            // This connection will immediately have the updated grants. Disabling read-only fails
            // on server2 instead.
            try_switchover("Failed to disable read_only on", mxt::ServerInfo::RUNNING);

            // server2 ends up with replication stopped, not an ideal situation. If auto-rejoin is on,
            // this is not an issue.
            test.tprintf("Rejoining server2");
            mxs.maxctrl("call command mariadbmon rejoin MySQL-Monitor server2");
            mxs.wait_for_monitor(1);
            mxs.check_print_servers_status({master, slave, slave, slave});

            test.tprintf("Switchover should now work.");
            try_switchover("", 0);

            mxs.check_print_servers_status({slave, master, slave, slave});
        }
    }

    if (!test.ok())
    {
        conn = master_srv->open_connection();
        conn->cmd_f("grant super, read_only admin on *.* to mariadbmon;");
    }
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
