/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-10-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include "mariadbmon_utils.hh"

// The test now runs only two failovers. Change for a longer time limit later.
// TODO: add semisync to remove this limitation.

namespace
{

void list_servers(TestConnections& test)
{
    test.print_maxctrl("list servers");
}

void run(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    testclient::Settings sett;
    sett.host = mxs.ip4();
    sett.port = mxs.rwsplit_port;
    sett.user = mxs.user_name();
    sett.pw = mxs.password();
    sett.rows = 100;

    testclient::ClientGroup clients(test, 4, sett);
    clients.prepare();

    if (test.ok())
    {
        clients.start();
        sleep(1);
        int failovers = 0;

        for (int i = 0; i < 3 && test.ok(); i++)
        {
            auto servers = mxs.get_servers();
            servers.print();
            auto master = servers.get_master();
            bool have_master = master.server_id > 0;
            int slaves = servers.get_role_info().slaves;

            if (have_master && slaves >= 1)
            {
                // Can do another failover.
                test.tprintf("Stopping master '%s'", master.name.c_str());
                int old_master_ind = master.server_id - 1;
                repl.stop_node(old_master_ind);
                mxs.sleep_and_wait_for_monitor(2, 3);

                // Failover should have happened, check.
                auto servers_after = mxs.get_servers();
                auto new_master = servers_after.get_master();
                if (new_master.server_id >= 0 && new_master.server_id != master.server_id)
                {
                    failovers++;
                    test.tprintf("Failover %i successfull.", failovers);
                }
                else if (new_master.server_id >= 0)
                {
                    test.add_failure("Master did not change, '%s' is still master.", new_master.name.c_str());
                }
                else
                {
                    test.add_failure("Failover didn't happen, no master.");
                }
                test.tprintf("Starting old master '%s'", master.name.c_str());
                repl.start_node(old_master_ind);
                sleep(1);
            }
            else if (have_master)
            {
                test.tprintf("No more slaves to promote, cannot continue.");
            }
            else
            {
                test.tprintf("No master, cannot continue");
            }
        }

        test.expect(failovers >= 3, "Expected at least 3 failovers, but only managed %i.", failovers);
        mxs.wait_for_monitor();
        clients.stop();
    }
    clients.cleanup();

    // Restore servers.
    auto servers = mxs.get_servers();
    auto roles = servers.get_role_info();
    if (roles.masters == 1 && roles.slaves == 3)
    {
        if (servers.get(0).status == mxt::ServerInfo::master_st)
        {
            // server1 is already master, no need to anything
        }
        else
        {
            mxs.maxctrl("call command mariadbmon switchover MySQL-Monitor server1");
        }
    }
    else
    {
        // Replication broken.
        mxs.maxctrl("call command mariadbmon reset-replication MySQL-Monitor server1");
    }

    mxs.wait_for_monitor(2);
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, run);
}
