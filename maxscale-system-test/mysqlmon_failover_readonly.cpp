/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "testconnections.h"
#include "fail_switch_rejoin_common.cpp"
#include <iostream>
#include <string>
#include <vector>

using std::string;
using std::cout;

int main(int argc, char** argv)
{
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);
    // Delete binlogs to sync gtid:s
    delete_slave_binlogs(test);
    // Test uses 2 slaves, stop the last one to prevent it from replicating anything.
    test.repl->stop_node(3);
    // Set up test table
    basic_test(test);
    // Advance gtid:s a bit to so gtid variables are updated.
    MYSQL* maxconn = test.maxscales->open_rwsplit_connection(0);
    generate_traffic_and_check(test, maxconn, 1);
    test.repl->sync_slaves(0);
    get_output(test);
    print_gtids(test);

    auto expect_server_status = [&test](const string& server_name, const string& status) {
            bool found = (test.maxscales->get_server_status(server_name.c_str()).count(status) == 1);
            test.expect(found, "%s was not %s as was expected.", server_name.c_str(), status.c_str());
        };

    string server_names[] = {"server1", "server2", "server3"};
    auto expect_server_status_multi =
        [&test, &expect_server_status, &server_names](std::vector<string> expected) {
            int expected_size = expected.size();
            test.expect(expected_size <= test.repl->N && expected_size <= 3, "Too many expected values.");
            int tests = (expected_size < test.repl->N) ? expected_size : test.repl->N;
            for (int node = 0; node < tests; node++)
            {
                expect_server_status(server_names[node], expected[node]);
            }
        };

    auto expect_read_only = [&test](int node, bool expected) {
            test.expect(test.repl->connect(node) == 0, "Connection to node %i failed.", node);
            char result[2];
            const char query[] = "SELECT @@read_only;";
            if (find_field(test.repl->nodes[node], query, "@@read_only", result) == 0)
            {
                char expected_result = expected ? '1' : '0';
                test.expect(result[0] == expected_result, "read_only on node %i was %c when %c was expected.",
                            node, result[0], expected_result);
            }
            else
            {
                test.expect(false, "Query '%s' failed on node %i.", query, node);
            }
        };

    auto expect_read_only_multi = [&test, &expect_read_only](std::vector<bool> expected) {
            int expected_size = expected.size();
            test.expect(expected_size <= test.repl->N, "Too many expected values.");
            int tests = (expected_size < test.repl->N) ? expected_size : test.repl->N;
            for (int node = 0; node < tests; node++)
            {
                expect_read_only(node, expected[node]);
            }
        };

    auto mon_wait = [&test](int ticks) {
            test.maxscales->wait_for_monitor(ticks);
        };

    auto crash_node = [&test](int node) {
            test.repl->ssh_node(node, "kill -s 11 `pidof mysqld`", true);
            test.repl->stop_node(node); // To prevent autostart.
        };

    string master = "Master";
    string slave = "Slave";
    string down = "Down";

    cout << "Step 1: All should be cool.\n";
    get_output(test);
    expect_server_status_multi({master, slave, slave});
    expect_read_only_multi({false, true, true});

    if (test.ok())
    {
        cout << "Step 2: Crash slave 2.\n";
        crash_node(2);
        mon_wait(1);
        get_output(test);
        expect_server_status_multi({master, slave, down});
        expect_read_only_multi({false, true});
        generate_traffic_and_check(test, maxconn, 2);

        cout << "Step 2.1: Slave 2 comes back up, check that read_only is set.\n";
        test.repl->start_node(2, "");
        mon_wait(2);
        get_output(test);
        expect_server_status_multi({master, slave, slave});
        expect_read_only_multi({false, true, true});
        generate_traffic_and_check(test, maxconn, 3);

        cout << "Step 3: Slave 1 crashes.\n";
        crash_node(1);
        mon_wait(1);
        get_output(test);
        expect_server_status_multi({master, down, slave});
        expect_read_only(2, true);
        generate_traffic_and_check(test, maxconn, 4);

        cout << "Step 4: Slave 2 goes down again, this time normally.\n";
        test.repl->stop_node(2);
        mon_wait(1);
        get_output(test);
        expect_server_status_multi({master, down, down});
        generate_traffic_and_check(test, maxconn, 5);

        cout << "Step 4.1: Slave 1 comes back up, check that read_only is set.\n";
        test.repl->start_node(1, "");
        mon_wait(2);
        get_output(test);
        expect_server_status_multi({master, slave, down});
        expect_read_only_multi({false, true});
        generate_traffic_and_check(test, maxconn, 6);

        cout << "Step 4.2: Slave 2 is back up, all should be well.\n";
        test.repl->start_node(2, "");
        mon_wait(2);
        get_output(test);
        expect_server_status_multi({master, slave, slave});
        expect_read_only_multi({false, true, true});
        generate_traffic_and_check(test, maxconn, 5);
    }
    mysql_close(maxconn);

    // Intermission, quit if a test step failed.
    if (test.ok())
    {
        // Some of the following tests depend on manipulating backends during the same monitor tick or
        // between ticks. Slow down the monitor to make this more likely. Not fool-proof in the slightest.
        int rval = test.maxscales->execute_maxadmin_command(0, "alter monitor MariaDB-Monitor "
                                                               "monitor_interval=4000");
        test.expect(rval == 0, "MaxAdmin command failed.");
    }

    if (test.ok())
    {
        cout << "Step 5: Master crashes but comes back during the next loop,"
                " slave 1 should be promoted, old master rejoined.\n";
        crash_node(0);
        mon_wait(1);    // The timing is probably a bit iffy here.
        expect_server_status(server_names[0], down);
        get_output(test);
        test.repl->start_node(0, "");
        mon_wait(2);
        get_output(test);
        // Slave 2 could be promoted as well, but in this case there is no reason to choose it.
        expect_server_status_multi({slave, master, slave});
        expect_read_only_multi({true, false, true});
        maxconn = test.maxscales->open_rwsplit_connection(0);
        generate_traffic_and_check(test, maxconn, 4);

        cout << "Step 6: Servers 1 & 3 go down. Server 2 should remain as master.\n";
        test.repl->stop_node(0);
        test.repl->stop_node(2);
        mon_wait(1);
        get_output(test);
        expect_server_status_multi({down, master, down});
        generate_traffic_and_check(test, maxconn, 3);

        cout << "Step 6.1: Servers 1 & 3 come back. Check that read_only is set.\n";
        test.repl->start_node(2, "");
        test.repl->start_node(0, "");
        mon_wait(2);
        get_output(test);
        expect_server_status_multi({slave, master, slave});
        expect_read_only_multi({true, false, true});
        generate_traffic_and_check(test, maxconn, 2);

        cout << "Step 7: Servers 1 & 2 go down. Check that 3 is promoted.\n";
        mysql_close(maxconn);
        test.repl->stop_node(0);
        test.repl->stop_node(1);
        mon_wait(2);
        get_output(test);
        expect_server_status_multi({down, down, master});
        maxconn = test.maxscales->open_rwsplit_connection(0);
        generate_traffic_and_check(test, maxconn, 1);
        mysql_close(maxconn);
    }

    
    // Start the servers, in case they weren't on already.
    for (int i = 0; i < 3; i++)
    {
        test.repl->start_node(i);
    }
    sleep(1);
    test.repl->connect();
    // Delete the test table from all databases, reset replication.
    const char drop_query[] = "DROP TABLE IF EXISTS test.t1;";
    for (int i = 0; i < 3; i++)
    {
        test.try_query(test.repl->nodes[i], drop_query);
    }
    test.maxscales->execute_maxadmin_command(0, "call command mariadbmon reset-replication "
                                                "MariaDB-Monitor server1");
    return test.global_result;
}
