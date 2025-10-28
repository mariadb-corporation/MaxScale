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

namespace
{
void test_main(TestConnections& test)
{
    auto& repl = *test.repl;
    auto& mxs = *test.maxscale;

    auto down = mxt::ServerInfo::DOWN;

    auto wait_for_master = [&test, &mxs]() {
        int master_id = -1;
        for (int i = 0; i < 10; i++)
        {
            sleep(1);
            auto servers = mxs.get_servers();
            servers.print();
            auto master_srv = servers.get_master();
            if (master_srv.is_master())
            {
                master_id = master_srv.server_id;
                test.tprintf("%s is master.", master_srv.name.c_str());
                break;
            }
        }
        test.expect(master_id > 0, "Failover not performed.");
        return master_id;
    };
    auto wait_for_rejoin = [&test, &mxs](int ind, bool must_succeed) {
        bool joined = false;
        for (int i = 0; i < 10; i++)
        {
            sleep(1);
            auto servers = mxs.get_servers();
            servers.print();
            auto rejoin_srv = servers.get(ind);
            if (rejoin_srv.is_slave())
            {
                joined = true;
                test.tprintf("%s rejoined the cluster.", rejoin_srv.name.c_str());
                break;
            }
        }
        if (!joined)
        {
            const char msg[] = "%s did not rejoin the cluster.";
            if (must_succeed)
            {
                test.add_failure(msg, test.repl->backend(ind)->vm_node().name());
            }
            else
            {
                test.tprintf(msg, test.repl->backend(ind)->vm_node().name());
            }
        }
        return joined;
    };

    // Check semisync is off when starting.
    semisync::check_semisync_off(test);
    create_test_user(test);

    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        test.tprintf("Setting up semisync replication.");
        semisync::setup_semisync_replication(test);

        test.tprintf("Preparing test clients.");
        testclient::Settings client_settings;
        client_settings.host = mxs.ip4();
        client_settings.port = mxs.rwsplit_port;
        client_settings.user = test_user_un();
        client_settings.pw = test_user_pw();
        client_settings.rows = 50;

        testclient::ClientGroup clients(test, 5, client_settings);
        clients.prepare();

        test.tprintf("Starting test clients.");
        clients.start();
        sleep(2);
        auto servers = mxs.get_servers();
        servers.print();

        if (test.ok())
        {
            test.tprintf("Test failover by shutting down the master.");
            const int master_ind = 0;
            int first_master_id = servers.get(master_ind).server_id;

            repl.stop_node(master_ind);
            mxs.wait_for_monitor();
            servers = mxs.get_servers();
            servers.print();
            auto old_master_info = servers.get(master_ind);
            test.expect(old_master_info.status == down, "Server %s should be down.",
                        old_master_info.name.c_str());
            int second_master_id = wait_for_master();

            const char master_no_change[] = "Master server id did not change.";

            test.expect(second_master_id != first_master_id, master_no_change);
            test.tprintf("Start %s, it should rejoin.", old_master_info.name.c_str());
            repl.start_node(master_ind);
            wait_for_rejoin(master_ind, true);
            servers = mxs.get_servers();
            servers.print();

            test.tprintf("Switchover back to server1.");
            mxs.maxctrlf("call command mariadbmon async-switchover MariaDB-Monitor server1");
            mxs.wait_for_monitor();
            wait_for_master();
            mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

            if (test.ok())
            {
                test.tprintf("Block master. Wait for failover. This may cause master to diverge as "
                             "init-rpl-rol does not protect against lost connections. The master will "
                             "eventually commit the diverging trx. Repeat this test up to 5 times.");

                int old_master_id = 1;
                int new_master_id = 0;

                for (int i = 0; i < 5 && test.ok(); i++)
                {
                    int old_master_index = old_master_id - 1;
                    repl.block_node(old_master_index);
                    mxs.wait_for_monitor();
                    servers = mxs.get_servers();
                    old_master_info = servers.get(old_master_index);
                    test.expect(old_master_info.status == down, "%s should be down.",
                                old_master_info.name.c_str());

                    new_master_id = wait_for_master();
                    test.expect(new_master_id != old_master_id, master_no_change);

                    test.tprintf("Unblock old master, it should rejoin assuming it did not diverge.");
                    repl.unblock_node(old_master_index);
                    bool joined = wait_for_rejoin(old_master_index, false);
                    servers = mxs.get_servers();
                    servers.print();
                    old_master_info = servers.get(old_master_index);
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

        clients.stop();
        mxs.wait_for_monitor(2);
        clients.print_stats();
        clients.cleanup();
        semisync::restore_normal_replication(test);
    }

    drop_test_user(test);
    semisync::check_semisync_off(test);
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}
