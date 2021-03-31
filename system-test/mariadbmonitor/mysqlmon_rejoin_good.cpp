/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "fail_switch_rejoin_common.cpp"

using std::string;

int main(int argc, char** argv)
{
    MariaDBCluster::require_gtid(true);
    TestConnections test(argc, argv);
    // Set up test table
    basic_test(test);
    // Delete binlogs to sync gtid:s
    delete_slave_binlogs(test);

    auto& mxs = test.maxscale();
    // Advance gtid:s a bit to so gtid variables are updated.
    MYSQL* maxconn = test.maxscales->open_rwsplit_connection(0);
    generate_traffic_and_check(test, maxconn, 10);
    mysql_close(maxconn);
    test.tprintf(LINE);
    mxs.wait_monitor_ticks();
    mxs.get_servers().print();
    mxs.check_servers_status(mxt::ServersInfo::default_repl_states());
    auto old_master = test.get_repl_master();
    test.expect(old_master, "No master at start.");
    mxt::MariaDBServer* new_master = nullptr;

    if (test.ok())
    {
        test.tprintf("Stopping master and waiting for failover. Check that another server is promoted.");
        test.tprintf(LINE);
        old_master->stop_database();
        mxs.wait_monitor_ticks(2);
        new_master = test.get_repl_master();
        test.expect(new_master && new_master != old_master, "Master did not change or no master detected.");

        string gtid_final_master;
        if (test.ok())
        {
            test.tprintf("'%s' is new master.", new_master->name().c_str());
            test.tprintf("Sending more inserts.");
            maxconn = test.maxscales->open_rwsplit_connection(0);
            generate_traffic_and_check(test, maxconn, 5);
            mxs.wait_monitor_ticks(1);
            auto status_before_rejoin = mxs.get_servers();
            status_before_rejoin.print();
            gtid_final_master = status_before_rejoin.get(new_master->name()).gtid;
            string gtid_old_master_before = status_before_rejoin.get(old_master->name()).gtid;
            test.expect(gtid_final_master != gtid_old_master_before, "Old master is still replicating.");
        }

        test.tprintf("Bringing old master back online. It should rejoin the cluster and catch up in events.");
        test.tprintf(LINE);
        old_master->start_database();
        mxs.wait_monitor_ticks(2);

        if (test.ok())
        {
            auto status_after_rejoin = mxs.get_servers();
            status_after_rejoin.print();
            string gtid_old_master_after = status_after_rejoin.get(old_master->name()).gtid;
            test.expect(gtid_final_master == gtid_old_master_after,
                        "Old master did not successfully rejoin the cluster.");

            test.tprintf("Switchover back to server1");
            mxs.maxctrl("call command mysqlmon switchover MySQL-Monitor server1 server2");
            mxs.wait_monitor_ticks(2);
            mxs.check_servers_status(mxt::ServersInfo::default_repl_states());
        }
    }

    return test.global_result;
}
