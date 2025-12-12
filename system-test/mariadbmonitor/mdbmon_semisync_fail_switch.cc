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
#include <maxbase/format.hh>

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

bool server_roles_match(const mxt::ServersInfo& info1, const mxt::ServersInfo& info2)
{
    if (info1.size() != info2.size())
    {
        return false;
    }
    bool rval = true;
    for (size_t i = 0; i < info1.size(); i++)
    {
        if (info1.get(i).status != info2.get(i).status)
        {
            rval = false;
            break;
        }
    }
    return rval;
}

void test_main(TestConnections& test)
{
    using namespace cooperative_monitoring;
    auto& repl = *test.repl;
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

    mxs1.wait_for_monitor();
    mxs2.wait_for_monitor();
    mxs1.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    mxs2.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        test.tprintf("Setting up semisync replication.");
        semisync::setup_semisync_replication(test);

        // Ensure mxs1 is primary, primary mxs may have changed during semisync setup.
        mxs1.stop();
        mxs2.stop();
        mxs1.start();
        mxs1.wait_for_monitor();
        mxs2.start();
        mxs2.wait_for_monitor();

        auto* primary_mon = &mon1;
        auto* secondary_mon = &mon2;
        test.expect(monitor_is_primary(test, *primary_mon), "Wrong primary monitor when starting test");
        int master_server_ind = 0;

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
            std::random_device dev;
            std::mt19937 rand_gen;
            rand_gen.seed(dev());
            std::uniform_int_distribution<int> action_gen(1, 100);

            time_t test_duration = 60;
            test.tprintf("Starting test clients and testing failover/switchover for %li seconds.",
                         test_duration);

            clients1.start();
            clients2.start();
            const time_t start = time(NULL);
            int primary_maxscale_failovers = 0;
            int master_server_failovers = 0;
            sleep(1);
            int iteration = 1;

            while (test.ok() && (time(NULL) - start < test_duration))
            {
                // 40%: kill both
                // 10%: only kill primary MaxScale
                // 50%: only kill primary server


                bool kill_master = false;
                bool kill_mxs = false;

                if (iteration <= 3)
                {
                    // During first iterations, kill both to ensure we get a valid test.
                    kill_master = true;
                    kill_mxs = true;
                }
                else
                {
                    int rand_num = action_gen(rand_gen);
                    if (rand_num <= 40)
                    {
                        kill_master = true;
                        kill_mxs = true;
                    }
                    else if (rand_num <= 60)
                    {
                        kill_mxs = true;
                    }
                    else
                    {
                        kill_master = true;
                    }
                }

                test.tprintf("Iteration %i. Kill primary MaxScale: %b, Kill primary server: %b",
                             iteration, kill_mxs, kill_master);

                auto kill_process = [&test](mxt::Node& node, const char* proc){
                    test.tprintf("Masking and killing process '%s' on %s.", proc, node.name());
                    // Prevents auto-restart after kill
                    auto res = node.run_cmd_output_sudof("sudo systemctl mask %s", proc);
                    test.expect(res.rc == 0, "Mask command on process '%s' failed.", proc);

                    string kill_cmd = mxb::string_printf("pkill --signal 11 %s", proc);
                    node.run_cmd_output_sudo(kill_cmd);

                    bool kill_confirmed = false;
                    string grep = mxb::string_printf("pgrep %s", proc);
                    for (int i = 0; i < 5; ++i)
                    {
                        // pgrep returns 1 for non-existing processes.
                        if (node.run_cmd_output(grep).rc)
                        {
                            kill_confirmed = true;
                            break;
                        }
                        else
                        {
                            // Try killing again, perhaps it will work...
                            node.run_cmd_output_sudo(kill_cmd);
                            sleep(1);
                        }
                    }

                    test.expect(kill_confirmed, "'%s' is still running!", proc);

                    // Stop service normally and enable starting it.
                    node.run_cmd_output_sudof("sudo systemctl stop %s", proc);
                    node.run_cmd_output_sudof("sudo systemctl unmask %s", proc);
                    int rc = node.run_cmd_output(grep).rc;
                    test.expect(rc, "'%s' started after unmasking.", proc);
                };

                MaxScale* killed_mxs = nullptr;
                if (kill_mxs)
                {
                    mxb_assert(primary_mon != secondary_mon);
                    test.expect(monitor_is_primary(test, *primary_mon),
                                "Wrong primary monitor when preparing to kill MaxScale.");
                    test.expect(!monitor_is_primary(test, *secondary_mon),
                                "Wrong secondary monitor when preparing to kill MaxScale.");
                    kill_process(primary_mon->maxscale->vm_node(), "maxscale");
                    primary_mon->maxscale->stop_and_check_stopped();
                    killed_mxs = primary_mon->maxscale;
                }

                int killed_master_ind = -1;
                if (kill_master)
                {
                    auto& node = repl.backend(master_server_ind)->vm_node();
                    kill_process(node, "mariadbd");
                    repl.stop_node(master_server_ind);      // To prevent autostart.
                    killed_master_ind = master_server_ind;
                }

                // Processes killed. Wait for both MaxScale and master server failover. MaxScale first.
                if (kill_mxs)
                {
                    // Some extra sleeps required, as in cooperative_monitoring test.
                    time_t mxs_swap_start = time(nullptr);
                    test.tprintf("Waiting until %s gets primary status.",
                                 secondary_mon->maxscale->node_name().c_str());
                    bool mxs_swapped = false;
                    do
                    {
                        if (monitor_is_primary(test, *secondary_mon))
                        {
                            test.tprintf("%s is primary MaxScale.",
                                         secondary_mon->maxscale->node_name().c_str());
                            std::swap(primary_mon, secondary_mon);
                            primary_mon->maxscale->wait_for_monitor();
                            mxs_swapped = true;
                        }
                        else if (time(nullptr) - mxs_swap_start > 15)
                        {
                            test.add_failure("%s did not get primary status within the time limit.",
                                             secondary_mon->maxscale->node_name().c_str());
                            break;
                        }
                        else
                        {
                            test.tprintf("%s is not yet primary, waiting...",
                                         secondary_mon->maxscale->node_name().c_str());
                            secondary_mon->maxscale->sleep_and_wait_for_monitor(1, 1);
                        }
                    }
                    while (!mxs_swapped);

                    if (mxs_swapped)
                    {
                        primary_maxscale_failovers++;
                        test.tprintf("Primary MaxScale failover %i success.", primary_maxscale_failovers);
                    }
                }

                if (kill_master)
                {
                    test.tprintf("Waiting for master server failover.");
                    master_server_ind = wait_for_master(primary_mon->maxscale);
                    if (master_server_ind >= 0)
                    {
                        master_server_failovers++;
                        test.tprintf("Master server failover %i success.", master_server_failovers);
                    }
                }

                test.tprintf("Restarting killed processes.");
                if (killed_master_ind >= 0)
                {
                    master_server_failovers++;
                    repl.start_node(killed_master_ind);
                }
                if (killed_mxs)
                {
                    killed_mxs->start_and_check_started();
                    killed_mxs->wait_for_monitor();
                    if (monitor_is_primary(test, *secondary_mon))
                    {
                        test.add_failure("%s is still the primary MaxScale.",
                                         secondary_mon->maxscale->node_name().c_str());
                    }
                }
                if (killed_master_ind >= 0)
                {
                    wait_for_rejoin(primary_mon->maxscale, killed_master_ind, true);
                }

                auto check_server_roles = [&]() {
                    mxs1.wait_for_monitor();
                    mxs2.wait_for_monitor();
                    auto srvs1 = mxs1.get_servers();
                    auto srvs2 = mxs2.get_servers();

                    bool rval = false;
                    if (server_roles_match(srvs1, srvs2))
                    {
                        srvs1.print();
                        rval = true;
                    }
                    else
                    {
                        test.add_failure("The two MaxScales see different server roles.");
                        srvs1.print();
                        srvs2.print();
                    }
                    return rval;
                };

                if (check_server_roles())
                {
                    if (action_gen(rand_gen) > 80)
                    {
                        test.tprintf("Test switchover on %s.", primary_mon->maxscale->node_name().c_str());
                        int next_master_ind = (master_server_ind + 1) % repl.N;
                        primary_mon->maxscale->maxctrlf(
                            "-t 20s call command mariadbmon switchover MariaDB-Monitor server%i",
                            next_master_ind + 1);
                        primary_mon->maxscale->wait_for_monitor(2);
                        check_server_roles();
                        master_server_ind = next_master_ind;
                    }
                }
                sleep(1);
                iteration++;
            }

            test.tprintf("Primary MaxScale failovers: %i, master server failovers: %i",
                         primary_maxscale_failovers, master_server_failovers);

            int min_expected_primary_mxs_swaps = 3;
            test.expect(primary_maxscale_failovers >= min_expected_primary_mxs_swaps,
                        "Expected at least %i failovers, but only managed %i.",
                        min_expected_primary_mxs_swaps, primary_maxscale_failovers);

            int min_expected_failovers = 3;     // The number of failovers is random, so keep small.
            test.expect(master_server_failovers >= min_expected_failovers,
                        "Expected at least %i failovers, but only managed %i.",
                        min_expected_failovers, master_server_failovers);

            if (test.ok())
            {
                // Breaks replication, so should be last part of test. Ensure that mxs1 is primary before
                // continuing.
                mxs2.stop_and_check_stopped();
                mxs1.wait_for_monitor(3);
                test.expect(monitor_is_primary(test, mon1), "MaxScale1 is not primary.");
                mxs2.start_and_check_started();
                if (test.ok())
                {
                    test_master_block(test, &mxs1);
                }
            }

            clients1.stop();
            clients2.stop();
        }
        clients1.print_stats();
        clients1.cleanup();

        clients2.print_stats();
        clients2.cleanup();

        semisync::restore_normal_replication(test);
        mxs1.wait_for_monitor();
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
