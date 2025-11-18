/*
 * Copyright (c) 2025 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-03-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include "mariadbmon_utils.hh"

using std::string;
using mxt::MaxScale;

namespace
{
void test_master_block(TestConnections& test, MaxScale* mxs);

int wait_for_master(mxt::MaxScale* mxs)
{
    int master_ind = -1;
    for (int i = 0; i < 10; i++)
    {
        sleep(1);
        auto servers = mxs->get_servers();
        servers.print();
        auto master_srv = servers.get_master();
        if (master_srv.is_master())
        {
            master_ind = master_srv.server_id - 1;
            mxs->log().log_msgf("%s is master.", master_srv.name.c_str());
            break;
        }
    }
    mxs->log().expect(master_ind >= 0, "Failover not performed.");
    return master_ind;
}

bool wait_for_rejoin(mxt::MaxScale* mxs, int ind, bool must_succeed)
{
    bool joined = false;
    for (int i = 0; i < 10; i++)
    {
        sleep(1);
        auto servers = mxs->get_servers();
        servers.print();
        auto rejoin_srv = servers.get(ind);
        if (rejoin_srv.is_slave())
        {
            joined = true;
            mxs->log().log_msgf("%s rejoined the cluster.", rejoin_srv.name.c_str());
            break;
        }
    }
    if (!joined)
    {
        const char msg[] = "%s did not rejoin the cluster.";
        auto name = mxs->get_servers().get(ind).name;
        if (must_succeed)
        {
            mxs->log().add_failure(msg, name.c_str());
        }
        else
        {
            mxs->log().log_msgf(msg, name.c_str());
        }
    }
    return joined;
}

void test_main(TestConnections& test)
{
    using namespace cooperative_monitoring;
    auto& mxs1 = *test.maxscale;
    auto& mxs2 = *test.maxscale2;
    MonitorInfo mon1{1, "MariaDB-Monitor", &mxs1};
    MonitorInfo mon2{2, "MariaDB-Monitor", &mxs2};

    test.expect(test.n_maxscales() >= 2, "At least 2 MaxScales are needed for this test. Exiting");
    if (!test.ok())
    {
        return;
    }

    // Check semisync is off when starting.
    semisync::check_semisync_off(test);
    create_test_user(test);

    // Ensure mxs1 is primary.
    mxs1.stop();
    mxs2.stop();
    mxs1.start();
    mxs1.wait_for_monitor();
    mxs2.start();
    mxs2.wait_for_monitor();

    mxs1.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    mxs2.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    auto* primary_mon = &mon1;
    test.expect(monitor_is_primary(test, *primary_mon), "Wrong primary monitor when starting test");

    if (test.ok())
    {
        test.tprintf("Setting up semisync replication.");
        semisync::setup_semisync_replication(test);

        mxs1.wait_for_monitor();
        mxs2.wait_for_monitor();
        test.tprintf("Preparing test clients.");
        auto conn = mxs1.open_rwsplit_connection2();
        conn->cmd_f("create or replace database test;");
        conn = nullptr;

        testclient::Settings client_sett1;
        client_sett1.host = mxs1.ip4();
        client_sett1.port = mxs1.rwsplit_port;
        client_sett1.user = test_user_un();
        client_sett1.pw = test_user_pw();
        client_sett1.rows = 50;
        client_sett1.group_name = "A";
        client_sett1.updates_pc = 30;
        client_sett1.trx_pc = 30;

        testclient::Settings client_sett2;
        client_sett2.host = mxs2.ip4();
        client_sett2.port = mxs2.rwsplit_port;
        client_sett2.user = test_user_un();
        client_sett2.pw = test_user_pw();
        client_sett2.rows = 50;
        client_sett2.group_name = "B";
        client_sett2.updates_pc = 30;
        client_sett2.trx_pc = 30;

        testclient::ClientGroup clients1(test, 10, client_sett1);
        testclient::ClientGroup clients2(test, 10, client_sett2);
        clients1.prepare();
        clients2.prepare();

        if (test.ok())
        {
            clients1.start();
            clients2.start();

            // Breaks replication, so should be last part of test.
            test_master_block(test, primary_mon->maxscale);

            clients1.stop();
            clients2.stop();
        }
        clients1.print_stats();
        clients1.cleanup();

        clients2.print_stats();
        clients2.cleanup();

        semisync::restore_normal_replication(test);
    }

    drop_test_user(test);
    semisync::check_semisync_off(test);
}

void test_master_block(TestConnections& test, MaxScale* mxs)
{
    auto down = mxt::ServerInfo::DOWN;
    auto& repl = *test.repl;
    int master_ind = wait_for_master(mxs);

    test.tprintf("Test failover by shutting down the master.");
    repl.stop_node(master_ind);
    mxs->wait_for_monitor();
    auto servers = mxs->get_servers();
    servers.print();

    auto old_master_info = servers.get(master_ind);
    test.expect(old_master_info.status == down, "Server %s should be down.",
                old_master_info.name.c_str());
    int second_master_ind = wait_for_master(mxs);

    const char master_no_change[] = "Master server did not change.";

    test.expect(second_master_ind != master_ind, master_no_change);
    test.tprintf("Start %s, it should rejoin.", old_master_info.name.c_str());
    repl.start_node(master_ind);
    wait_for_rejoin(mxs, master_ind, true);
    servers = mxs->get_servers();
    servers.print();

    test.tprintf("Switchover back to server1.");
    mxs->maxctrlf("call command mariadbmon async-switchover MariaDB-Monitor server1");
    mxs->wait_for_monitor();
    wait_for_master(mxs);
    mxs->check_print_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        test.tprintf("Block master. Wait for failover. This may cause master to diverge as "
                     "init-rpl-role does not protect against lost connections. The master will "
                     "eventually commit the diverging trx. Repeat this test up to 5 times.");

        int old_master_ind = second_master_ind;
        int new_master_ind = -1;

        for (int i = 0; i < 5 && test.ok(); i++)
        {
            repl.block_node(old_master_ind);
            mxs->wait_for_monitor();
            servers = mxs->get_servers();
            old_master_info = servers.get(old_master_ind);
            test.expect(old_master_info.status == down, "%s should be down.",
                        old_master_info.name.c_str());

            new_master_ind = wait_for_master(mxs);
            test.expect(new_master_ind != old_master_ind, master_no_change);

            test.tprintf("Unblock old master, it should rejoin assuming it did not diverge.");
            repl.unblock_node(old_master_ind);
            bool joined = wait_for_rejoin(mxs, old_master_ind, false);
            servers = mxs->get_servers();
            servers.print();
            old_master_info = servers.get(old_master_ind);
            if (joined)
            {
                test.tprintf("Iteration %i, rejoin success.", i + 1);
            }
            else
            {
                test.tprintf("Iteration %i, rejoin failed.", i + 1);

                auto& sconns = old_master_info.slave_connections;
                if (sconns.empty())
                {
                    test.add_failure("No slave connections on %s.", old_master_info.name.c_str());
                }
                else
                {
                    auto sconn = old_master_info.slave_connections[0];
                    test.tprintf("Rejoin of %s failed.", old_master_info.name.c_str());
                    using IO_State = mxt::ServerInfo::SlaveConnection::IO_State;
                    string io_str = (sconn.io_running == IO_State::YES) ? "Yes" :
                        (sconn.io_running == IO_State::CONNECTING) ? "Connecting" : "No";
                    string sql_str = sconn.sql_running ? "Yes" : "No";
                    test.tprintf("IO running: %s, SQL running: %s", io_str.c_str(), sql_str.c_str());
                }

                test.tprintf("Ending test.");
                break;
            }
        }
    }
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    test.reset_timeout(600);
    return test.run_test(argc, argv, test_main);
}
