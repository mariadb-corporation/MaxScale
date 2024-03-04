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

/**
 * MXS-359: Starting sessions without master
 *
 * https://jira.mariadb.org/browse/MXS-359
 */
#include <maxtest/testconnections.hh>
#include <vector>
#include <iostream>
#include <sstream>

using std::cout;
using std::endl;

struct TestCase
{
    const char* description;
    void        (* func)(TestConnections&);
};

TestConnections* global_test;

void change_master(int next, int current)
{
    TestConnections& test = *global_test;
    test.maxctrl("stop monitor MySQL-Monitor");
    test.repl->connect();
    test.repl->change_master(next, current);
    test.repl->close_connections();
    test.maxctrl("start monitor MySQL-Monitor");

    // Blocking the node makes sure the monitor picks the new master
    test.repl->block_node(current);
    test.maxscale->wait_for_monitor();
    test.repl->unblock_node(current);
    test.maxscale->wait_for_monitor();
}

void test_replaced_master(TestConnections& test)
{
    test.log_printf("Sanity check that reads and writes work");
    test.maxscale->connect_rwsplit();
    test.try_query(test.maxscale->conn_rwsplit, "INSERT INTO test.t1 VALUES (1)");
    test.try_query(test.maxscale->conn_rwsplit, "SELECT * FROM test.t1");

    test.repl->block_node(0);
    test.maxscale->wait_for_monitor();

    test.log_printf("Reads should still work even if no master is available");
    test.try_query(test.maxscale->conn_rwsplit, "SELECT * FROM test.t1");

    test.repl->unblock_node(0);
    change_master(1, 0);
    test.maxscale->wait_for_monitor();

    test.log_printf("Reads and writes after master change should work");
    test.try_query(test.maxscale->conn_rwsplit, "INSERT INTO test.t1 VALUES (2)");
    test.try_query(test.maxscale->conn_rwsplit, "SELECT * FROM test.t1");

    test.maxscale->disconnect();
    change_master(0, 1);
}

void test_new_master(TestConnections& test)
{
    test.log_printf("Block the master before connecting");
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor();

    test.log_printf("Connect and check that read-only mode works");
    test.maxscale->connect_rwsplit();
    test.try_query(test.maxscale->conn_rwsplit, "SELECT * FROM test.t1");

    change_master(1, 0);
    test.maxscale->wait_for_monitor(2);

    test.log_printf("Both reads and writes after master change should work");
    test.try_query(test.maxscale->conn_rwsplit, "INSERT INTO test.t1 VALUES (2)");
    test.try_query(test.maxscale->conn_rwsplit, "SELECT * FROM test.t1");

    test.repl->unblock_node(0);
    test.maxscale->disconnect();
    change_master(0, 1);
}

void test_master_failure(TestConnections& test)
{
    test.log_printf("Sanity check that reads and writes work");
    test.maxscale->connect_rwsplit();
    test.try_query(test.maxscale->conn_rwsplit, "INSERT INTO test.t1 VALUES (1)");
    test.try_query(test.maxscale->conn_rwsplit, "SELECT * FROM test.t1");

    test.repl->block_node(0);
    test.maxscale->wait_for_monitor();

    test.log_printf("Reads should still work even if no master is available");
    test.try_query(test.maxscale->conn_rwsplit, "SELECT * FROM test.t1");

    test.log_printf("Writes should fail");
    int rc = execute_query_silent(test.maxscale->conn_rwsplit, "INSERT INTO test.t1 VALUES (1)");
    test.expect(rc != 0, "Write after master failure should fail");

    test.repl->unblock_node(0);
    test.maxscale->disconnect();
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    global_test = &test;

    std::vector<TestCase> tests(
    {
        {"test_replaced_master", test_replaced_master},
        {"test_new_master", test_new_master},
        {"test_master_failure", test_master_failure}
    });

    for (auto& i : tests)
    {
        // Create a table for testing
        test.maxscale->connect_rwsplit();
        test.try_query(test.maxscale->conn_rwsplit, "CREATE OR REPLACE TABLE test.t1(id INT)");
        test.repl->sync_slaves();
        test.maxscale->disconnect();

        std::stringstream out;
        test.log_printf("Running test: %s", i.description);
        i.func(test);
        if (!test.ok())
        {
            break;
        }

        // Wait for the monitoring to stabilize before dropping the table
        test.maxscale->sleep_and_wait_for_monitor(2, 2);

        test.maxscale->connect_rwsplit();
        test.try_query(test.maxscale->conn_rwsplit, "DROP TABLE test.t1");
        test.maxscale->disconnect();

        test.repl->fix_replication();
    }

    return test.global_result;
}
