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

using std::string;
using std::cout;

int main(int argc, char** argv)
{
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);

    auto expect_server_status = [&test](const string& server_name, const string& status) {
            bool found = (test.maxscales->get_server_status(server_name.c_str()).count(status) == 1);
            test.expect(found, "%s was not %s as was expected.", server_name.c_str(), status.c_str());
        };

    auto expect_not_server_status = [&test](const string& server_name, const string& status) {
            bool not_found = (test.maxscales->get_server_status(server_name.c_str()).count(status) == 0);
            test.expect(not_found, "%s was %s contrary to expectation.", server_name.c_str(), status.c_str());
        };

    auto read_sum = [&test](int server_ind) -> int {
            int sum = -1;
            const char query[] = "SELECT SUM(c1) FROM test.t1;";
            const char field[] = "SUM(c1)";
            char value[100];
            if ((find_field(test.repl->nodes[server_ind], query, field, value) == 0) && strlen(value) > 0)
            {
                sum = atoi(value);
            }
            return sum;
        };

    const char insert_query[] = "INSERT INTO test.t1 VALUES (%i);";
    const char drop_query[] = "DROP TABLE test.t1;";
    const char strict_mode[] = "SET GLOBAL gtid_strict_mode=%i;";

    string server_names[] = {"server1", "server2", "server3", "server4"};
    string master = "Master";
    string slave = "Slave";

    // Set up test table
    MYSQL* maxconn = test.maxscales->open_rwsplit_connection(0);
    test.tprintf("Creating table and inserting data.");
    test.try_query(maxconn, "CREATE OR REPLACE TABLE test.t1(c1 INT)");
    int insert_val = 1;
    test.try_query(maxconn, insert_query, insert_val++);
    cout << "Setting gitd_strict_mode to ON.\n";
    test.try_query(maxconn, strict_mode, 1);
    test.repl->sync_slaves();
    mysql_close(maxconn);

    get_output(test);
    print_gtids(test);
    expect_server_status(server_names[0], master);
    expect_server_status(server_names[1], slave);
    expect_server_status(server_names[2], slave);
    expect_server_status(server_names[3], slave);

    // Stop MaxScale and mess with the nodes.
    cout << "Inserting events directly to nodes while MaxScale is stopped.\n";
    test.maxscales->stop_maxscale();
    test.repl->connect();
    // Modify the databases of backends identically. This will unsync gtid:s but not the actual data.
    for (; insert_val <= 9; insert_val++)
    {
        // When inserting data, start from the slaves so replication breaks immediately.
        test.try_query(test.repl->nodes[1], insert_query, insert_val);
        test.try_query(test.repl->nodes[2], insert_query, insert_val);
        test.try_query(test.repl->nodes[3], insert_query, insert_val);
        test.try_query(test.repl->nodes[0], insert_query, insert_val);
    }
    // Restart MaxScale, there should be no slaves. Master is still ok.
    test.maxscales->start_maxscale();
    test.maxscales->wait_for_monitor(2);
    cout << "Restarted MaxScale.\n";
    print_gtids(test);
    get_output(test);

    expect_server_status(server_names[0], master);
    expect_not_server_status(server_names[1], slave);
    expect_not_server_status(server_names[2], slave);
    expect_not_server_status(server_names[3], slave);

    if (test.global_result == 0)
    {
        // Use the reset-replication command to magically fix the situation.
        cout << "Running reset-replication to fix the situation.\n";
        test.maxscales->execute_maxadmin_command(0, "call command mariadbmon reset-replication "
                                                    "MySQL-Monitor server2");
        test.maxscales->wait_for_monitor(1);
        // Add another event to force gtid forward.
        maxconn = test.maxscales->open_rwsplit_connection(0);
        test.try_query(maxconn, "FLUSH TABLES;");
        test.try_query(maxconn, insert_query, insert_val);
        mysql_close(maxconn);

        test.maxscales->wait_for_monitor(1);
        get_output(test);
        expect_server_status(server_names[0], slave);
        expect_server_status(server_names[1], master);
        expect_server_status(server_names[2], slave);
        expect_server_status(server_names[3], slave);
        // Check that the values on the databases are identical by summing the values.
        int expected_sum = 55;      // 11 * 5
        for (int i = 0; i < test.repl->N; i++)
        {
            int sum = read_sum(i);
            test.expect(sum == expected_sum,
                        "The values in server%i are wrong, sum is %i when %i was expected.",
                        i + 1, sum, expected_sum);
        }

        // Finally, switchover back and erase table
        cout << "Running switchover.\n";
        test.maxscales->execute_maxadmin_command(0, "call command mariadbmon switchover MySQL-Monitor");
        test.maxscales->wait_for_monitor(1);
        get_output(test);
        expect_server_status(server_names[0], master);
        expect_server_status(server_names[1], slave);
        expect_server_status(server_names[2], slave);
        expect_server_status(server_names[3], slave);
    }

    if (test.global_result != 0)
    {
        test.repl->fix_replication();
    }
    maxconn = test.maxscales->open_rwsplit_connection(0);
    test.try_query(maxconn, strict_mode, 0);
    test.try_query(maxconn, drop_query);
    mysql_close(maxconn);
    return test.global_result;
}
