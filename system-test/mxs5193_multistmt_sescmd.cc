/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

void multistmt_sescmd(TestConnections& test)
{
    auto r = test.repl->get_connection(0);
    test.expect(r.connect()
                && r.query("CREATE OR REPLACE TABLE test.t1(id INT)")
                && r.query("CREATE USER bob IDENTIFIED BY 'bob'")
                && r.query("GRANT ALL ON *.* TO bob"),
                "Failed to set up test: %s", r.error());
    test.repl->sync_slaves();

    auto c = test.maxscale->rwsplit();
    c.set_credentials("bob", "bob");

    c.connect();
    c.query("SET autocommit=1; INSERT INTO test.t1 VALUES (1);");
    test.repl->sync_slaves();

    auto num_slave_rows = c.field("SELECT COUNT(*) FROM test.t1");
    test.expect(num_slave_rows == "1", "Expected 1 row on the slave but got: %s", num_slave_rows.c_str());

    // Kill the connection, session command history replay should then take place.
    r.query("KILL USER bob");

    auto num_master_rows = c.field("SELECT COUNT(*), @@last_insert_id FROM test.t1");
    test.expect(num_master_rows == "1", "Expected 1 row on the master but got: %s", num_master_rows.c_str());

    num_slave_rows = c.field("SELECT COUNT(*) FROM test.t1");
    test.expect(num_slave_rows == "1", "Expected 1 row on the slave but got: %s", num_slave_rows.c_str());

    r.query("DROP TABLE test.t1");
    r.query("DROP USER bob");
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, multistmt_sescmd);
}
