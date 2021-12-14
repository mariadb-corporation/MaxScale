/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
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

void run_failover_test(TestConnections& test, int old_master, int new_master)
{
    std::vector<const char*> srv = {"server1", "server2"};

    auto master = test.maxscale->get_servers().get_master();
    test.expect(master.name == srv[old_master],
                "'%s' should be Master, not '%s'", srv[old_master], master.name.c_str());

    // Block the node, it should fail over to server2. Wait more than the failcount to make sure the master
    // switch happens.
    test.repl->block_node(old_master);
    test.maxscale->wait_for_monitor(4);

    master = test.maxscale->get_servers().get_master();
    test.expect(master.name == srv[new_master],
                "'%s' should be Master, not '%s'", srv[new_master], master.name.c_str());

    // Unblock the node
    test.repl->unblock_node(old_master);
    test.maxscale->wait_for_monitor(4);


    // The old slave should now be the new master
    auto servers = test.maxscale->get_servers();
    master = servers.get_master();
    test.expect(master.name == srv[new_master],
                "'%s' should still be Master, not '%s'", srv[new_master], master.name.c_str());
    test.expect(servers.get(old_master).status & mxt::ServerInfo::SLAVE,
                "Expected '%s' to be Slave but it is not.", srv[old_master]);

    // The new master should have two replication streams configured
    auto conn_new = test.repl->get_connection(new_master);
    conn_new.connect();
    auto streams = conn_new.rows("SHOW ALL SLAVES STATUS");
    test.expect(streams.size() == 2,
                "Expected 2 replication streams on '%s', found %lu", srv[new_master], streams.size());

    // The old one should only have one
    auto conn_old = test.repl->get_connection(old_master);
    conn_old.connect();
    streams = conn_old.rows("SHOW ALL SLAVES STATUS");

    test.expect(streams.size() == 1,
                "Expected 1 replication streams on '%s', found %lu", srv[old_master], streams.size());
}

void test_multisource_replication(TestConnections& test)
{
    test.tprintf("Test failover with external multi-source replication");

    // Stop the monitor to prevent it from undoing the changes
    test.check_maxctrl("unlink monitor MariaDB-Monitor server3 server4");
    test.check_maxctrl("stop monitor MariaDB-Monitor");

    const char* sql =
        R"(
CHANGE MASTER 'first' TO MASTER_HOST='%s', MASTER_PORT=3306, MASTER_USER='repl', MASTER_PASSWORD='repl', MASTER_USE_GTID=CURRENT_POS;
CHANGE MASTER 'second' TO MASTER_HOST='%s', MASTER_PORT=3306, MASTER_USER='repl', MASTER_PASSWORD='repl', MASTER_USE_GTID=CURRENT_POS;
START SLAVE 'first';
START SLAVE 'second';
)";

    auto conn = test.repl->get_connection(0);
    conn.connect();
    bool ok = conn.query(mxb::string_printf(sql, test.repl->ip(2), test.repl->ip(3)));
    test.expect(ok, "Failed to configure replication: %s", conn.error());

    test.check_maxctrl("start monitor MariaDB-Monitor");
    test.maxscale->wait_for_monitor(2);

    if (test.ok())
    {
        run_failover_test(test, 0, 1);
        run_failover_test(test, 1, 0);
    }

    // Fix replication
    test.check_maxctrl("link monitor MariaDB-Monitor server3 server4");
    test.check_maxctrl("call command mariadbmon reset-replication MariaDB-Monitor server1");
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

        if (test.ok())
        {
            test_multisource_replication(test);
        }

        test.repl->connect();
        for (int i = 0; i < n; i++)
        {
            test.try_query(test.repl->nodes[i], set_ac, 1);
        }
    }
}
