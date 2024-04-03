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
    auto maxconn = mxs.open_rwsplit_connection2("test");
    // Advance gtid:s a bit to so gtid variables are updated.
    generate_traffic_and_check(test, maxconn.get(), 10);

    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        test.tprintf("Stopping master and waiting for failover. Check that another server is promoted.");
        test.repl->stop_node(0);

        // Wait until failover is performed
        test.maxscale->wait_for_monitor(2);
        mxs.check_print_servers_status({mxt::ServerInfo::DOWN, mxt::ServerInfo::master_st,
                                        mxt::ServerInfo::slave_st, mxt::ServerInfo::slave_st});

        if (test.ok())
        {
            // Recreate maxscale session
            maxconn = mxs.open_rwsplit_connection2("test");
            test.tprintf("Sending more inserts.");
            generate_traffic_and_check(test, maxconn.get(), 5);
            mxs.get_servers().print();

            if (test.ok())
            {
                test.tprintf("Bring old master back online...");
                test.repl->start_node(0);
                mxs.wait_for_monitor(2);
                test.tprintf("and manually rejoin it to cluster.");

                mxs.maxctrl("call command mariadbmon rejoin MariaDB-Monitor server1");
                mxs.wait_for_monitor(2);

                auto status = mxs.get_servers();
                status.print();
                status.check_servers_status({mxt::ServerInfo::slave_st, mxt::ServerInfo::master_st,
                                             mxt::ServerInfo::slave_st, mxt::ServerInfo::slave_st});
                test.expect(status.get(0).gtid == status.get(1).gtid, "Old master didn't catch up.");
            }
        }

        test.repl->start_node(0);

        // Switch master back to server1.
        mxs.maxctrl("call command mariadbmon switchover MariaDB-Monitor server1 server2");
        test.maxscale->wait_for_monitor(2);
        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

        if (test.ok())
        {
            // STOP and RESET SLAVE on a server, then remove binlogs. Check that a server with empty binlogs
            // can be rejoined.
            test.tprintf("Removing slave connection and deleting binlogs on server3 to get empty gtid.");
            int slave_to_reset = 2;
            test.repl->connect();
            auto conn = test.repl->backend(slave_to_reset)->open_connection();
            conn->cmd("STOP SLAVE;");
            conn->cmd("RESET SLAVE ALL;");
            conn->cmd("RESET MASTER;");
            conn->cmd("SET GLOBAL gtid_slave_pos='';");
            mxs.wait_for_monitor();
            mxs.check_print_servers_status({mxt::ServerInfo::master_st, mxt::ServerInfo::slave_st,
                                            mxt::ServerInfo::RUNNING, mxt::ServerInfo::slave_st});

            auto res = conn->simple_query("SELECT @@gtid_current_pos;");
            test.expect(res.empty(), "server3 gtid is not empty as it should (%s).", res.c_str());

            test.tprintf("Rejoining server3.");
            mxs.maxctrl("call command mysqlmon rejoin MariaDB-Monitor server3");
            mxs.wait_for_monitor(2);
            mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
        }
    }
}
