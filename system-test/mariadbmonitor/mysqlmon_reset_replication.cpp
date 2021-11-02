/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <iostream>
#include <string>
#include <maxtest/mariadb_connector.hh>

using std::string;
using std::cout;

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto read_sum = [&test](int server_ind) {
            int rval = -1;
            auto conn = test.repl->backend(server_ind)->open_connection();
            auto res = conn->query("SELECT SUM(c1) FROM test.t1;");
            if (res && res->next_row() && res->get_col_count() == 1)
            {
                rval = res->get_int(0);
            }
            return rval;
        };

    const char insert_query[] = "INSERT INTO test.t1 VALUES (%i);";
    const char drop_query[] = "DROP TABLE test.t1;";
    const char strict_mode[] = "SET GLOBAL gtid_strict_mode=%i;";

    const int N = 4;

    auto& mxs = *test.maxscale;

    // Set up test table
    auto maxconn = mxs.open_rwsplit_connection2();
    test.tprintf("Creating table and inserting data.");
    maxconn->cmd("CREATE OR REPLACE TABLE test.t1(c1 INT)");
    int insert_val = 1;
    maxconn->cmd_f(insert_query, insert_val++);
    test.tprintf("Setting gitd_strict_mode to ON.");
    maxconn->cmd_f(strict_mode, 1);
    test.repl->sync_slaves();

    auto status = mxs.get_servers();
    status.print();
    status.check_servers_status(mxt::ServersInfo::default_repl_states());

    // Stop MaxScale and mess with the nodes.
    test.tprintf("Inserting events directly to nodes while MaxScale is stopped.");
    mxs.stop();
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
    mxs.start();
    mxs.wait_for_monitor(2);
    status = mxs.get_servers();
    status.print();

    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    auto running = mxt::ServerInfo::RUNNING;
    status.check_servers_status({mxt::ServerInfo::master_st, running, running, running});

    if (test.ok())
    {
        // Use the reset-replication command to magically fix the situation.
        test.tprintf("Running reset-replication to fix the situation.");
        test.maxctrl("call command mariadbmon reset-replication MySQL-Monitor server2");
        mxs.wait_for_monitor();
        // Add another event to force gtid forward.
        maxconn = mxs.open_rwsplit_connection2();
        maxconn->cmd("FLUSH TABLES;");
        maxconn->cmd_f(insert_query, insert_val);

        mxs.wait_for_monitor();
        status = mxs.get_servers();
        status.print();
        status.check_servers_status({slave, master, slave, slave});

        // Check that the values on the databases are identical by summing the values.
        int expected_sum = 55;      // 11 * 5
        for (int i = 0; i < N; i++)
        {
            int sum = read_sum(i);
            test.expect(sum == expected_sum,
                        "The values in server%i are wrong, sum is %i when %i was expected.",
                        i + 1, sum, expected_sum);
        }

        // Finally, switchover back and erase table
        test.tprintf("Running switchover.");
        mxs.maxctrl("call command mariadbmon switchover MySQL-Monitor");
        mxs.wait_for_monitor();
        status = mxs.get_servers();
        status.print();
        status.check_servers_status(mxt::ServersInfo::default_repl_states());
    }

    maxconn = mxs.open_rwsplit_connection2();
    maxconn->cmd_f(strict_mode, 0);
    maxconn->cmd(drop_query);
}
