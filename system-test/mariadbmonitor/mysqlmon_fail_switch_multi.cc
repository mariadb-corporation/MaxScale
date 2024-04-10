/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <string>
#include <maxsql/mariadb_connector.hh>
#include "mariadbmon_utils.hh"

// Test failover/switchover with multiple masters.

using std::string;

namespace
{
void change_master(MariaDBCluster& repl, int slave, int master, const string& conn_name = "",
                   int replication_delay = 0);
void reset_master(MariaDBCluster& repl, int slave, const string& conn_name = "");
void expect_replicating_from(TestConnections& test, int node, int master);
}

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    auto down = mxt::ServerInfo::DOWN;
    auto running = mxt::ServerInfo::RUNNING;
    auto ext_master = mxt::ServerInfo::EXT_MASTER;

    const string secondary_slave_conn = "b";
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    auto mon_wait = [&test](int ticks) {
        test.maxscale->wait_for_monitor(ticks);
    };

    // Add a few events. Needs to be replicated to all servers.
    auto maxconn = mxs.open_rwsplit_connection2();
    generate_traffic_and_check(test, maxconn.get(), 5);

    // Only monitoring two servers for now. Stop replication to non-monitored servers.
    reset_master(repl, 2);
    reset_master(repl, 3);

    test.tprintf("Step 1: All should be cool.");
    mxs.check_print_servers_status({master, slave});

    if (test.ok())
    {
        test.tprintf("Step 2: External replication to two servers");
        change_master(repl, 0, 2);
        change_master(repl, 0, 3, secondary_slave_conn);
        mon_wait(1);

        mxs.check_print_servers_status({master | ext_master, slave});
        expect_replicating_from(test, 0, 2);
        expect_replicating_from(test, 0, 3);
    }

    if (test.ok())
    {
        test.tprintf("Step 3: Failover. Check that new master replicates from external servers.");
        repl.stop_node(0);
        mon_wait(2);

        mxs.check_print_servers_status({down, master | ext_master});
        expect_replicating_from(test, 1, 2);
        expect_replicating_from(test, 1, 3);

        // Generate traffic and check again.
        auto conn = repl.backend(2)->open_connection();
        generate_traffic_and_check(test, conn.get(), 2);
        mxs.check_print_servers_status({down, master | ext_master});
    }

    if (test.ok())
    {
        test.tprintf("Step 4: Bring up old master, it should not rejoin.");
        repl.start_node(0);
        mon_wait(2);    // Should not rejoin since has multiple slave connections.
        mxs.check_print_servers_status({running | ext_master, master | ext_master});

        test.tprintf("Step 5: Remove slave connections from old master, see that it rejoins.");
        reset_master(repl, 0);
        reset_master(repl, 0, secondary_slave_conn);
        mon_wait(2);
        mxs.check_print_servers_status({slave, master | ext_master});

        mxs.maxctrl("call command mariadbmon switchover MariaDB-Monitor");
        mon_wait(2);

        // Generate traffic and check again.
        auto conn = repl.backend(3)->open_connection();
        generate_traffic_and_check(test, conn.get(), 2);

        mxs.check_print_servers_status({master | ext_master, slave});
        expect_replicating_from(test, 0, 2);
        expect_replicating_from(test, 0, 3);
    }

    if (test.ok())
    {
        // Cleanup.
        reset_master(repl, 0);
        reset_master(repl, 0, secondary_slave_conn);
        mon_wait(1);
        mxs.check_print_servers_status({master, slave});
        change_master(repl, 2, 0);
        change_master(repl, 3, 0);
    }
    else
    {
        // If something went wrong, delete test db from all backends and reset replication.
        repl.ping_or_open_admin_connections();
        for (int i = 0; i < 3; i++)
        {
            repl.backend(i)->admin_connection()->cmd("DROP TABLE IF EXISTS test.t1;");
        }
        mxs.maxctrl("call command mariadbmon reset-replication MariaDB-Monitor server1");
    }
}

namespace
{
void change_master(MariaDBCluster& repl, int slave, int master, const string& conn_name,
                   int replication_delay)
{
    const char query[] = "CHANGE MASTER '%s' TO master_host='%s', master_port=%d, "
                         "MASTER_USE_GTID = current_pos, master_user='repl', master_password='repl', "
                         "master_delay=%d;";
    auto be = repl.backend(slave);
    be->ping_or_open_admin_connection();
    be->admin_connection()->cmd_f(query, conn_name.c_str(), repl.ip4(master), repl.port(master),
                                  replication_delay);
    be->admin_connection()->cmd_f("START SLAVE '%s';", conn_name.c_str());
}

void reset_master(MariaDBCluster& repl, int slave, const string& conn_name)
{
    auto be = repl.backend(slave);
    be->ping_or_open_admin_connection();
    be->admin_connection()->cmd_f("STOP SLAVE '%s';", conn_name.c_str());
    be->admin_connection()->cmd_f("RESET SLAVE '%s' ALL;", conn_name.c_str());
}

void expect_replicating_from(TestConnections& test, int node, int master)
{
    auto& repl = *test.repl;
    bool found = false;
    auto N = repl.N;
    if (node < N && master < N)
    {
        auto be = repl.backend(node);
        be->ping_or_open_admin_connection();
        auto conn = be->admin_connection();
        auto res = conn->query("SHOW ALL SLAVES STATUS;");
        if (res)
        {
            auto search_host = repl.ip(master);
            auto search_port = repl.port(master);
            while (res->next_row())
            {
                auto host = res->get_string("Master_Host");
                auto port = res->get_int("Master_Port");
                if (host == search_host && port == search_port)
                {
                    found = true;
                    break;
                }
            }
        }
    }

    test.expect(found, "Server %i is not replicating from server %i.", node + 1, master + 1);
}
}
