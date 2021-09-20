/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include "fail_switch_rejoin_common.cpp"
#include <iostream>
#include <string>
#include <vector>
#include <maxsql/queryresult.hh>
#include <maxsql/mariadb_connector.hh>

// Test failover/switchover with multiple masters.

using std::string;
using std::cout;
using mxq::MariaDBQueryResult;

void change_master(TestConnections& test ,int slave, int master, const string& conn_name = "",
                   int replication_delay = 0);

void reset_master(TestConnections& test ,int slave, const string& conn_name = "");
void expect_replicating_from(TestConnections& test, int node, int master);

string server_names[] = {"server1", "server2", "server3", "server4"};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    //delete_slave_binlogs(test);
    // Set up test table
    basic_test(test);
    // Advance gtid:s a bit to so gtid variables are updated.
    MYSQL* maxconn = test.maxscale->open_rwsplit_connection();
    generate_traffic_and_check(test, maxconn, 1);
    test.repl->sync_slaves(0);
    get_output(test);
    print_gtids(test);

    auto expect_server_status = [&test](const string& server_name, const string& status) {
        bool found = (test.maxscale->get_server_status(server_name).count(status) == 1);
        test.expect(found, "%s was not %s as was expected.", server_name.c_str(), status.c_str());
    };

    auto expect_server_status_multi =
            [&test, &expect_server_status](std::vector<string> expected) {
                int expected_size = expected.size();
                test.expect(expected_size <= test.repl->N && expected_size <= 3, "Too many expected values.");
                int tests = (expected_size < test.repl->N) ? expected_size : test.repl->N;
                for (int node = 0; node < tests; node++)
                {
                    expect_server_status(server_names[node], expected[node]);
                }
            };

    auto mon_wait = [&test](int ticks) {
        test.maxscale->wait_for_monitor(ticks);
    };

    string master = "Master";
    string slave = "Slave";
    string down = "Down";
    string relay = "Relay Master";
    string running = "Running";
    string ext_master = "Slave of External Server";

    const string secondary_slave_conn = "b";
    // Only monitoring two servers for now. Stop replication to non-monitored servers.
    reset_master(test, 2);
    reset_master(test, 3);

    cout << "Step 1: All should be cool.\n";
    get_output(test);
    expect_server_status_multi({master, slave});

    if (test.ok())
    {
        cout << "Step 2: External replication to two servers\n";
        change_master(test, 0, 2);
        change_master(test, 0, 3, secondary_slave_conn);
        mon_wait(1);
        get_output(test);
        expect_server_status_multi({master, slave});
        // TODO: readd
        // expect_server_status(server_names[0], ext_master);
        expect_replicating_from(test, 0, 2);
        expect_replicating_from(test, 0, 3);
    }

    if (test.ok())
    {
        cout << "Step 3: Failover. Check that new master replicates from external servers.\n";
        test.repl->stop_node(0);
        mon_wait(3);
	    get_output(test);
        expect_server_status_multi({down, master});
        // TODO: readd
        // expect_server_status(server_names[1], ext_master);
        expect_replicating_from(test, 1, 2);
        expect_replicating_from(test, 1, 3);
    }

    if (test.ok())
    {
        cout << "Step 4: Bring up old master, allow it to rejoin, then switchover. "
                "Check that new master replicates from external servers.\n";
        test.repl->start_node(0);
        mon_wait(2); // Should not rejoin since has multiple slave connections.
        expect_server_status_multi({running, master});
	    test.repl->connect();
        reset_master(test, 0);
        reset_master(test, 0, secondary_slave_conn);
        mon_wait(2);
	    get_output(test);
        expect_server_status_multi({slave, master});
        test.maxscale->ssh_output("maxctrl call command mariadbmon switchover MariaDB-Monitor");
        mon_wait(2);
        expect_replicating_from(test, 0, 2);
        expect_replicating_from(test, 0, 3);
	    get_output(test);

        // TODO: Extend test later
    }


    // Cleanup
    mysql_close(maxconn);
    test.repl->connect();
    // Delete the test table from all databases, reset replication.
    const char drop_query[] = "DROP TABLE IF EXISTS test.t1;";
    for (int i = 0; i < 3; i++)
    {
        test.try_query(test.repl->nodes[i], drop_query);
    }
    test.maxscale->ssh_output("maxctrl call command mariadbmon reset-replication MariaDB-Monitor server1");
    return test.global_result;
}

void change_master(TestConnections& test ,int slave, int master, const string& conn_name,
                   int replication_delay)
{
    const char query[] = "CHANGE MASTER '%s' TO master_host='%s', master_port=%d, "
                         "MASTER_USE_GTID = current_pos, "
                         "master_user='repl', master_password='repl', master_delay=%d; "
                         "START SLAVE '%s';";
    test.try_query(test.repl->nodes[slave], query, conn_name.c_str(),
                   test.repl->ip4(master), test.repl->port[master],
                   replication_delay, conn_name.c_str());
}

void reset_master(TestConnections& test ,int slave, const string& conn_name)
{
    const char query[] = "STOP SLAVE '%s'; RESET SLAVE '%s' ALL;";
    test.try_query(test.repl->nodes[slave], query, conn_name.c_str(), conn_name.c_str());
}

void expect_replicating_from(TestConnections& test, int node, int master)
{
    bool found = false;
    auto N = test.repl->N;
    if (node < N && master < N)
    {
        auto conn = test.repl->nodes[node];
        if (mysql_query(conn, "SHOW ALL SLAVES STATUS;") == 0)
        {
            auto res = mysql_store_result(conn);
            if (res)
            {
                mxq::MariaDBQueryResult q_res(res);
                auto search_host = test.repl->ip(master);
                auto search_port = test.repl->port[master];
                while (q_res.next_row())
                {
                    auto host = q_res.get_string("Master_Host");
		            auto port = q_res.get_int("Master_Port");
                    if (host == search_host && port == search_port)
                    {
                        found = true;
                        break;
                    }
                }
            }
        }
    }

    test.expect(found, "Server %s is not replicating from %s.",
	            server_names[node].c_str(), server_names[master].c_str());
}
