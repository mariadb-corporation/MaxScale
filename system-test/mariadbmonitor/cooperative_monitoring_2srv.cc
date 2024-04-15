/*
 * Copyright (c) 2024 MariaDB plc, Finnish Branch
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
#include <maxbase/string.hh>
#include "mariadbmon_utils.hh"

using std::string;
using namespace cooperative_monitoring;

namespace
{

void test_main(TestConnections& test)
{
    test.expect(test.n_maxscales() >= 2, "At least 2 MaxScales are needed for this test. Exiting");
    if (!test.ok())
    {
        return;
    }

    const auto master_slave = {mxt::ServerInfo::master_st, mxt::ServerInfo::slave_st};
    const auto slave_master = {mxt::ServerInfo::slave_st, mxt::ServerInfo::master_st};
    auto& mxs1 = *test.maxscale;
    auto& mxs2 = *test.maxscale2;
    auto& repl = *test.repl;

    mxs1.start_maxscale();
    // Ensure MaxScale1 gets locks.
    mxs1.wait_for_monitor(1);
    mxs2.start_maxscale();
    mxs2.wait_for_monitor(1);

    MonitorInfo monitors[] = {
        {1,  "MariaDB-Monitor"},
        {2,  "MariaDB-Monitor"},
        {-1, "none"           },
    };

    monitors[0].maxscale = &mxs1;
    monitors[1].maxscale = &mxs2;

    auto wait_both = [&monitors](int ticks) {
        for (int i = 0; i < ticks; i++)
        {
            monitors[0].maxscale->wait_for_monitor(1);
            monitors[1].maxscale->wait_for_monitor(1);
        }
    };

    const auto* primary_mon = get_primary_monitor(test, monitors);
    test.expect(primary_mon && primary_mon->id == 1, "MaxScale1 does not have exclusive lock.");

    mxs1.check_print_servers_status(master_slave);
    mxs2.check_print_servers_status(master_slave);

    if (test.ok())
    {
        test.tprintf("Stop master for 2 seconds, then bring it back. Primary MaxScale and master should "
                     "not change.");
        auto* srv1 = repl.backend(0);
        srv1->stop_database();
        sleep(2);
        srv1->start_database();
        mxs1.wait_for_monitor(2);
        mxs2.wait_for_monitor();

        primary_mon = get_primary_monitor(test, monitors);
        test.expect(primary_mon && primary_mon->id == 1,
                    "MaxScale1 does not have exclusive locks after server1 restart.");
        mxs1.check_print_servers_status(master_slave);
        mxs2.check_print_servers_status(master_slave);

        test.tprintf("Stop master for several monitor ticks, then bring it back. Server2 should get "
                     "promoted in the meantime.");
        srv1->stop_database();
        wait_both(4);

        for (int i = 0; i < 3; i++)
        {
            if (mxs1.get_servers().get(1).status == mxt::ServerInfo::master_st)
            {
                break;
            }
            sleep(1);
        }
        srv1->start_database();
        mxs1.wait_for_monitor(2);
        mxs2.wait_for_monitor();

        primary_mon = get_primary_monitor(test, monitors);
        test.expect(primary_mon && primary_mon->id == 1,
                    "MaxScale1 does not have exclusive lock after server1 failover.");
        mxs1.check_print_servers_status(slave_master);
        mxs2.check_print_servers_status(slave_master);

        if (test.ok())
        {
            test.log_printf("Block server2 and wait a few seconds. Primary monitor should not change. "
                            "Server1 should be promoted master.");
            int block_server_ind = 1;
            repl.block_node(block_server_ind);
            sleep(2);

            for (int i = 0; i < 5; i++)
            {
                wait_both(1);
                auto mon_info = monitors[0];
                test.expect(monitor_is_primary(test, mon_info),
                            "MaxScale %i does not have exclusive lock after server2 was blocked.",
                            mon_info.id);

                if (mxs1.get_servers().get(0).status == mxt::ServerInfo::master_st)
                {
                    break;
                }
            }

            auto master_down = {mxt::ServerInfo::master_st, mxt::ServerInfo::DOWN};
            mxs1.check_print_servers_status(master_down);
            mxs2.check_print_servers_status(master_down);

            test.tprintf("Confirm that master-lock is still taken on server2, as monitor connection was not "
                         "properly closed.");
            auto* srv2 = repl.backend(block_server_ind);
            string query = R"(SELECT IS_USED_LOCK(\"maxscale_mariadbmonitor_master\"))";
            auto res = srv2->vm_node().run_sql_query(query);
            test.tprintf("Query '%s' returned %i: '%s'", query.c_str(), res.rc, res.output.c_str());
            test.expect(res.rc == 0, "Query failed.");
            int conn_id = -1;
            mxb::get_int(res.output, &conn_id);
            if (conn_id > 0)
            {
                test.tprintf("Lock is still owned by connection %i.", conn_id);
            }
            else
            {
                test.add_failure("Invalid thread id or lock is free on server2.");
            }

            test.tprintf("Unblock server2. Now, neither MaxScale should have lock majority until "
                         "lock on server2 is freed. The previous primary MaxScale will release its locks as "
                         "it cannot be certain it has majority.");
            repl.unblock_node(block_server_ind);
            wait_both(1);

            for (int i = 0; i < 2; i++)
            {
                auto mon_info = monitors[i];
                if (monitor_is_primary(test, mon_info))
                {
                    test.add_failure("MaxScale %i is primary when none expected.", mon_info.id);
                }
                else
                {
                    test.tprintf("MaxScale %i is secondary.", mon_info.id);
                }
            }

            if (test.ok())
            {
                test.tprintf("Both MaxScales are now secondary and obey previous masterlock. Server2 "
                             "swaps to master again. This is not really what we would want but it is "
                             "what happens.");
                auto running_master = {mxt::ServerInfo::RUNNING, mxt::ServerInfo::master_st};
                mxs1.check_print_servers_status(running_master);
                mxs2.check_print_servers_status(running_master);
            }

            test.tprintf("Restart server2. It should stay master. Either MaxScale should get lock majority "
                         "and rejoin server1.");
            srv2->stop_database();
            srv2->start_database();
            sleep(2);
            wait_both(1);
            primary_mon = get_primary_monitor(test, monitors);
            test.expect(primary_mon, "No primary monitor.");
            if (primary_mon)
            {
                test.tprintf("MaxScale %i is primary and should rejoin server1 shortly.", primary_mon->id);
                primary_mon->maxscale->wait_for_monitor(2);
                wait_both(1);
                mxs1.check_print_servers_status(slave_master);
                mxs2.check_print_servers_status(slave_master);
            }
        }
    }
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    TestConnections::skip_maxscale_start(true);
    return test.run_test(argc, argv, test_main);
}
