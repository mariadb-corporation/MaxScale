/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
#include "mariadbmon_utils.hh"

using std::string;

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;

    auto maxconn = mxs.open_rwsplit_connection2();
    generate_traffic_and_check(test, maxconn.get(), 5);

    mxs.wait_for_monitor();
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    auto old_master = test.get_repl_master();
    test.expect(old_master, "No master at start.");
    mxt::MariaDBServer* new_master = nullptr;

    if (test.ok())
    {
        test.tprintf("Stopping master and waiting for failover. Check that another server is promoted.");
        old_master->stop_database();
        mxs.wait_for_monitor(2);
        new_master = test.get_repl_master();
        test.expect(new_master && new_master != old_master, "Master did not change or no master detected.");

        string gtid_final_master;
        if (test.ok())
        {
            test.tprintf("'%s' is new master. Sending more inserts.", new_master->cnf_name().c_str());
            maxconn = mxs.open_rwsplit_connection2();
            generate_traffic_and_check(test, maxconn.get(), 5);
            mxs.wait_for_monitor(1);
            auto status_before_rejoin = mxs.get_servers();
            status_before_rejoin.print();
            gtid_final_master = status_before_rejoin.get(new_master->cnf_name()).gtid;
            string gtid_old_master_before = status_before_rejoin.get(old_master->cnf_name()).gtid;
            test.expect(!gtid_final_master.empty() && !gtid_old_master_before.empty(), "Gtid error");
            test.expect(gtid_final_master != gtid_old_master_before, "Old master is still replicating.");
        }

        test.tprintf("Bringing old master back online. It should rejoin the cluster and catch up in events.");
        old_master->start_database();
        mxs.wait_for_monitor(2);

        if (test.ok())
        {
            auto status_after_rejoin = mxs.get_servers();
            status_after_rejoin.print();
            string gtid_old_master_after = status_after_rejoin.get(old_master->cnf_name()).gtid;
            test.expect(gtid_final_master == gtid_old_master_after,
                        "Old master did not successfully rejoin the cluster.");

            test.tprintf("Switchover back to server1");
            mxs.maxctrl("call command mysqlmon switchover MariaDB-Monitor server1 server2");
            mxs.wait_for_monitor(2);
            mxs.check_servers_status(mxt::ServersInfo::default_repl_states());
        }
    }
}
