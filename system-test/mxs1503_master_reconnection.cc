/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

/**
 * MXS-1503: Testing of master_reconnection and master_failure_mode=error_on_write
 *
 * https://jira.mariadb.org/browse/MXS-1503
 */
#include <maxtest/testconnections.hh>
#include <vector>
#include <iostream>
#include <functional>

using std::cout;
using std::endl;

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto query = [&test](std::string q) {
            return execute_query_silent(test.maxscale->conn_rwsplit, q.c_str());
        };

    auto error_matches = [&test](std::string q) {
            std::string err = mysql_error(test.maxscale->conn_rwsplit);
            return err.find(q) != std::string::npos;
        };

    auto block_master = [&test]() {
            test.repl->block_node(0);
            test.maxscale->wait_for_monitor();
        };

    auto unblock_master = [&test]() {
            test.repl->unblock_node(0);
            test.maxscale->wait_for_monitor();
        };

    test.maxscale->connect();
    test.expect(query("DROP TABLE IF EXISTS test.t1") == 0,
                "DROP TABLE should work.");
    test.expect(query("CREATE TABLE test.t1 (id INT)") == 0,
                "CREATE TABLE should work.");
    test.expect(query("INSERT INTO test.t1 VALUES (1)") == 0,
                "Write should work at the start of the test.");

    block_master();
    test.expect(query("INSERT INTO test.t1 VALUES (1)") != 0,
                "Write should fail after master is blocked.");

    test.expect(error_matches("read-only"),
                "Error should mention read-only mode");

    unblock_master();
    test.expect(query("INSERT INTO test.t1 VALUES (1)") == 0,
                "Write should work after unblocking master");

    query("DROP TABLE test.t1");

    return test.global_result;
}
