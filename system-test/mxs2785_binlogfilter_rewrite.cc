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
#include <sstream>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto slave = test.repl->get_connection(1);
    slave.connect();
    slave.query("STOP SLAVE");
    std::ostringstream ss;
    ss << "CHANGE MASTER TO MASTER_HOST='" << test.maxscale->ip()
       << "', MASTER_PORT=4008, MASTER_USE_GTID=slave_pos";
    slave.query(ss.str());

    auto master = test.repl->get_connection(0);
    master.connect();

    // Since the servers are configured to use ROW based replication, we only
    // use DDL statement to test. This makes sure they result in query events.
    master.query("CREATE DATABASE test_db1");
    master.query("CREATE TABLE test_db1.t1(id int)");
    master.query("USE test_db1");
    master.query("CREATE TABLE t2(id int)");

    master.query("CREATE DATABASE test_db2");
    master.query("CREATE TABLE test_db2.t1(id int)");
    master.query("USE test_db2");
    master.query("CREATE TABLE t2(id int)");

    master.query("CREATE DATABASE some_db");
    master.query("CREATE TABLE some_db.t1(id int)");
    master.query("USE some_db");
    master.query("CREATE TABLE t2(id int)");

    // Also test that the ignoring mechanism works
    master.query("CREATE DATABASE ignore_this");
    master.query("CREATE TABLE ignore_this.t1(id int)");
    master.query("INSERT INTO ignore_this.t1 VALUES(123)");

    master.query("CREATE TABLE test.ignore_this(id int)");
    master.query("INSERT INTO test.ignore_this VALUES(456)");

    slave.query("START SLAVE");
    slave.query("SELECT MASTER_GTID_WAIT('" + master.field("SELECT @@last_gtid") + "', 30)");

    // The filter does s/test_[a-z0-9_]*/$1_rewritten/g
    test.expect(slave.query("SELECT * FROM test_db1_rewritten.t1 LIMIT 1"),
                "Query to test_db1_rewritten.t1 should work: %s", slave.error());
    test.expect(slave.query("SELECT * FROM test_db1_rewritten.t2 LIMIT 1"),
                "Query to test_db1_rewritten.t2 should work: %s", slave.error());
    test.expect(slave.query("SELECT * FROM test_db2_rewritten.t1 LIMIT 1"),
                "Query to test_db2_rewritten.t1 should work: %s", slave.error());
    test.expect(slave.query("SELECT * FROM test_db2_rewritten.t2 LIMIT 1"),
                "Query to test_db2_rewritten.t2 should work: %s", slave.error());
    test.expect(slave.query("SELECT * FROM some_db.t1 LIMIT 1"),
                "Query to some_db.t1 should work: %s", slave.error());
    test.expect(slave.query("SELECT * FROM some_db.t2 LIMIT 1"),
                "Query to some_db.t2 should work: %s", slave.error());

    test.expect(!slave.query("SELECT * FROM ignore_this.t1"), "Query to ignore_this.t1 should fail");
    test.expect(!slave.query("SELECT * FROM test.ignore_this"), "Query to test.ignore_this should fail");

    auto c = test.repl->backend(1)->open_connection();
    auto res = c->query("SHOW SLAVE STATUS");

    while (res->next_row())
    {
        test.expect(res->get_string("Slave_IO_Running") == "Yes",
                    "Slave_IO_Running is not Yes: %s",
                    res->get_string("Last_IO_Error").c_str());
        test.expect(res->get_string("Slave_SQL_Running") == "Yes",
                    "Slave_SQL_Running is not Yes: %s",
                    res->get_string("Last_IO_Error").c_str());
    }

    master.query("DROP DATABASE ignore_this");
    master.query("DROP table test.ignore_this");

    master.query("DROP DATABASE test_db1");
    master.query("DROP DATABASE test_db2");
    master.query("DROP DATABASE some_db");

    slave.query("SELECT MASTER_GTID_WAIT('" + master.field("SELECT @@last_gtid") + "', 30)");

    return test.global_result;
}
