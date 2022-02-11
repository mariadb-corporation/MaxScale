/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <iostream>
#include <maxbase/format.hh>
#include <maxtest/mariadb_connector.hh>

using std::string;
using std::cout;

int get_master_server_id(TestConnections& test)
{
    MYSQL* conn = test.maxscale->open_rwsplit_connection();
    int id = -1;
    char str[1024];

    if (find_field(conn, "SELECT @@server_id, @@last_insert_id;", "@@server_id", str) == 0)
    {
        id = atoi(str);
    }

    mysql_close(conn);
    return id;
}

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    TestConnections test;
    TestConnections::skip_maxscale_start(true);
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto* repl = test.repl;
    const int n = repl->N;

    repl->connect();
    auto server_ids = repl->get_all_server_ids();
    // Check MaxScale is stopped. This is required to ensure no monitor journal exists.
    auto rwconn = mxs.try_open_rwsplit_connection();
    test.expect(!rwconn->is_open(), "MaxScale should be stopped.");

    // Stop the master and the last slave, then start MaxScale.
    int master_ind = 0;
    int last_slave_ind = 3;

    string master_name = mxb::string_printf("server%i", master_ind + 1);
    string slave_name = mxb::string_printf("server%i", last_slave_ind + 1);

    test.tprintf("Stopping %s and %s.", master_name.c_str(), slave_name.c_str());
    repl->stop_node(master_ind);
    repl->stop_node(last_slave_ind);

    test.tprintf("Starting MaxScale");
    mxs.start_and_check_started();

    sleep(1);
    mxs.wait_for_monitor(3);

    test.log_includes("Performing automatic failover");
    int new_master_id = mxs.get_master_server_id();
    int expected_id1 = server_ids[1];
    int expected_id2 = server_ids[2];
    test.expect(new_master_id == expected_id1 || new_master_id == expected_id2,
                "Unexpected master server id. Got %i when %i or %i was expected.",
                new_master_id, expected_id1, expected_id2);

    if (test.ok())
    {
        // Restart server4, check that it rejoins.
        test.repl->start_node(last_slave_ind);
        test.maxscale->wait_for_monitor(2);

        auto states = mxs.get_servers().get(slave_name);
        test.expect(states.status == mxt::ServerInfo::slave_st,
                    "%s is not replicating as it should.", slave_name.c_str());
    }

    if (test.ok())
    {
        // Finally, bring back old master and swap to it.
        test.repl->start_node(master_ind);
        test.maxscale->wait_for_monitor(2);

        test.tprintf("Switching back old master %s.", master_name.c_str());
        string switchover = "call command mariadbmon switchover MariaDB-Monitor " + master_name;
        test.maxctrl(switchover);
        test.maxscale->wait_for_monitor(2);
        new_master_id = mxs.get_master_server_id();
        test.expect(new_master_id == server_ids[master_ind], "Switchover to original master failed.");
    }

    if (test.ok())
    {
        // Test that switchover works even if autocommit is off on all backends.
        test.tprintf("Setting autocommit=0 on all backends, then check that switchover works.");
        test.maxscale->stop();
        test.repl->connect();
        const char set_ac[] = "SET GLOBAL autocommit=%i;";
        for (int i = 0; i < n; i++)
        {
            test.try_query(test.repl->nodes[i], set_ac, 0);
        }
        test.maxscale->start();

        // Check that autocommit is really off.
        Connection conn = test.repl->get_connection(2);
        conn.connect();
        auto row = conn.row("SELECT @@GLOBAL.autocommit;");
        test.expect(!row.empty() && row[0] == "0", "autocommit is not off");

        new_master_id = mxs.get_master_server_id();
        test.expect(new_master_id == server_ids[master_ind], "No valid master");

        if (test.ok())
        {
            test.tprintf("Switchover...");
            string switchover = "call command mariadbmon switchover MariaDB-Monitor";
            test.maxctrl(switchover);
            test.maxscale->wait_for_monitor(2);
            new_master_id = mxs.get_master_server_id();
            test.expect(new_master_id != server_ids[master_ind], "Switchover failed.");
            if (test.ok())
            {
                test.expect(new_master_id == server_ids[1], "Switchover to wrong server.");
            }

            switchover = "call command mariadbmon switchover MariaDB-Monitor " + master_name;
            test.maxctrl(switchover);
            test.maxscale->wait_for_monitor(2);
            mxs.check_servers_status(mxt::ServersInfo::default_repl_states());
        }

        test.repl->connect();
        for (int i = 0; i < n; i++)
        {
            test.try_query(test.repl->nodes[i], set_ac, 1);
        }
    }
}
