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
 * Test for MXS-1310.
 * - Only explicit databases used -> shard containing the explicit database
 * - Only implicit databases used -> shard containing current database
 * - Mix of explicit and implicit databases -> shard containing current database
 */

#include <maxtest/testconnections.hh>
#include <maxbase/string.hh>

void mxs4947(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());

    const char* query = "SELECT TABLE_NAME FROM information_schema.tables "
                        "WHERE TABLE_SCHEMA IN('db2', 'db3')";

    // Should go to the first node
    auto no_db = c.pretty_rows(query);
    mxb::trim(no_db);
    test.expect(no_db.empty(), "Expected no tables, got: '%s'", no_db.c_str());

    // Should go to the second node
    c.query("USE db2");
    auto using_db2 = c.pretty_rows(query);
    mxb::trim(using_db2);
    test.expect(using_db2 == "t2", "Expected 't2', got: '%s'", using_db2.c_str());

    // Should go to the third node
    c.query("USE db3");
    auto using_db3 = c.pretty_rows(query);
    mxb::trim(using_db3);
    test.expect(using_db3 == "t3", "Expected 't3', got: '%s'", using_db3.c_str());
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    // Get the @@server_id value from both shards
    char server_id[2][1024];
    test.repl->connect();
    sprintf(server_id[0], "%d", test.repl->get_server_id(0));
    sprintf(server_id[1], "%d", test.repl->get_server_id(1));
    execute_query(test.repl->nodes[0],
                  "CREATE DATABASE db1;"
                  "CREATE TABLE db1.t1(id int);"
                  "INSERT INTO db1.t1 VALUES (@@server_id)");
    execute_query(test.repl->nodes[1],
                  "CREATE DATABASE db2;"
                  "CREATE TABLE db2.t2(id int);"
                  "INSERT INTO db2.t2 VALUES (@@server_id)");
    execute_query(test.repl->nodes[2],
                  "CREATE DATABASE db3;"
                  "CREATE TABLE db3.t3(id int);"
                  "INSERT INTO db3.t3 VALUES (@@server_id)");
    test.repl->sync_slaves();

    test.tprintf("Run test with sharded database as active database");
    test.maxscale->connect_rwsplit();
    test.try_query(test.maxscale->conn_rwsplit, "USE db2");
    execute_query_check_one(test.maxscale->conn_rwsplit, "SELECT @@server_id, id FROM t2", server_id[1]);
    execute_query_check_one(test.maxscale->conn_rwsplit,
                            "SELECT @@server_id, id FROM db1.t1",
                            server_id[0]);
    execute_query_check_one(test.maxscale->conn_rwsplit,
                            "SELECT @@server_id, a.id FROM t2 as a JOIN db1.t1 as b",
                            server_id[1]);
    test.maxscale->close_rwsplit();

    test.tprintf("Run test with a common database as active database");
    test.maxscale->connect_rwsplit();
    test.try_query(test.maxscale->conn_rwsplit, "USE db1");
    execute_query_check_one(test.maxscale->conn_rwsplit, "SELECT @@server_id, id FROM t1", server_id[0]);
    execute_query_check_one(test.maxscale->conn_rwsplit,
                            "SELECT @@server_id, id FROM db2.t2",
                            server_id[1]);
    execute_query_check_one(test.maxscale->conn_rwsplit,
                            "SELECT @@server_id, a.id FROM t1 as a JOIN db1.t1 as b",
                            server_id[0]);
    test.maxscale->close_rwsplit();

    // MXS-4947: Tables in information_schema are treated as a normal tables
    // https://jira.mariadb.org/browse/MXS-4947
    mxs4947(test);

    //  Cleanup
    execute_query(test.repl->nodes[0], "DROP DATABASE db1");
    execute_query(test.repl->nodes[1], "DROP DATABASE db2");
    execute_query(test.repl->nodes[2], "DROP DATABASE db3");

    return test.global_result;
}
