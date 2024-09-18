/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-09-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-2520: Allow master reconnection on reads
 * https://jira.mariadb.org/browse/MXS-2520
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto server = test.repl->get_connection(0);
    test.expect(server.connect()
                && server.query("CREATE USER 'bob'@'%' IDENTIFIED BY 'bob'")
                && server.query("GRANT ALL ON *.* TO 'bob'@'%'")
                && server.query("CREATE OR REPLACE TABLE test.t1(id INT)")
                && server.query("INSERT INTO t1 VALUES (1), (2), (3)")
                && server.query("LOCK TABLE t1 WRITE"),
                "Failed to set up test: %s", server.error());

    auto rws = test.maxscale->rwsplit();
    rws.set_credentials("bob", "bob");
    test.expect(rws.connect()
                && rws.query("SELECT 1")    // Makes sure the connection is opened
                && rws.send_query("SELECT * FROM test.t1"),
                "Failed connect to rws: %s", rws.error());

    server.query("KILL USER bob");
    server.query("UNLOCK TABLES");

    test.expect(rws.read_query_result(),
                "Query should succeed even after connection failure: %s", rws.error());

    server.query("DROP USER 'bob'@'%'");
    server.query("DROP TABLE test.t1");

    return test.global_result;
}
