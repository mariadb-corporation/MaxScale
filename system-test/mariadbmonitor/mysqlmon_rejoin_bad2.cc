/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
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
    auto& repl = *test.repl;
    auto maxconn = mxs.open_rwsplit_connection2();
    generate_traffic_and_check(test, maxconn.get(), 5);
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    maxconn.reset();

    // Stop master, wait for failover
    test.tprintf("Stopping master, should auto-failover.");
    mxt::MariaDBServer* master_id_old = test.get_repl_master();
    repl.stop_node(0);
    mxs.wait_for_monitor(3);
    mxs.check_print_servers_status({mxt::ServerInfo::DOWN, mxt::ServerInfo::master_st,
                                    mxt::ServerInfo::slave_st, mxt::ServerInfo::slave_st});

    if (test.ok())
    {
        test.tprintf("Stopping MaxScale for a moment.");
        // Stop maxscale to prevent an unintended rejoin.
        mxs.stop_and_check_stopped();
        // Restart old master. Then add some events to it.
        test.tprintf("Restart node 0 and add more events.");
        repl.start_node(0);
        auto conn = repl.backend(0)->open_connection();
        generate_traffic_and_check_nosync(test, conn.get(), 5);

        test.tprintf("Starting MaxScale, node 0 should not be able to join because it has extra events.");
        mxs.start_and_check_started();
        mxs.sleep_and_wait_for_monitor(2, 1);
        mxs.check_print_servers_status({mxt::ServerInfo::RUNNING, mxt::ServerInfo::master_st,
                                        mxt::ServerInfo::slave_st, mxt::ServerInfo::slave_st});

        if (test.ok())
        {
            test.tprintf("Setting server 2 to replicate from server 1. Server 2 should lose its master "
                         "status and other servers should be redirected to server 1.");
            const char CHANGE_CMD_FMT[] = "CHANGE MASTER TO MASTER_HOST = '%s', MASTER_PORT = %d, "
                                          "MASTER_USE_GTID = current_pos, "
                                          "MASTER_USER='repl', MASTER_PASSWORD = 'repl';";
            conn = repl.backend(1)->open_connection();
            conn->cmd_f(CHANGE_CMD_FMT, repl.ip(0), repl.port(0));
            conn->cmd("START SLAVE;");
            mxs.wait_for_monitor(2);
            mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
        }
    }
}
