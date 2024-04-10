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

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.repl->execute_query_all_nodes("STOP SLAVE");
    test.maxscale->wait_for_monitor();
    auto node = test.repl->get_connection(0);

    test.tprintf("Create tables t1 and T1: they shuould be treated as the same table");

    test.expect(node.connect(), "Failed to connect: %s", node.error());
    test.expect(node.query("CREATE TABLE test.t1(id INT)"),
                "Failed to create `test` . `t1`: %s", node.error());
    test.expect(node.query("CREATE TABLE test.T1(id INT)"),
                "Failed to create `test` . `T1`: %s", node.error());

    auto rws = test.maxscale->rwsplit();
    test.expect(rws.connect(), "Failed to connect to readwritesplit: %s", rws.error());
    test.expect(rws.query("SELECT * FROM test.t1"), "Failed to query `test` . `t1`: %s", rws.error());
    test.expect(rws.query("SELECT * FROM test.T1"), "Failed to query `test` . `T1`: %s", rws.error());

    node.query("DROP TABLE test.t1");
    node.query("DROP TABLE test.T1");

    return test.global_result;
}
