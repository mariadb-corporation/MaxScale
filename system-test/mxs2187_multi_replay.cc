/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-03-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-1507: Test inconsistent result detection
 *
 * https://jira.mariadb.org/browse/MXS-1507
 */
#include <maxtest/testconnections.hh>
#include <functional>
#include <iostream>
#include <vector>

using namespace std;

#define EXPECT(a) test.expect(a, "%s%s%s", "Assertion failed: " #a, master.error(), c.error())

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    auto master = test.repl->get_connection(0);
    auto c = test.maxscale->rwsplit();
    EXPECT(master.connect());

    // Create a table
    EXPECT(master.query("DROP TABLE IF EXISTS test.t1"));
    EXPECT(master.query("CREATE TABLE test.t1(id INT)"));

    // Create a user
    const string NAME = "mxs2187_multi_replay";
    EXPECT(master.query("CREATE USER " + NAME + " IDENTIFIED BY '" + NAME + "'"));
    EXPECT(master.query("GRANT ALL ON *.* TO " + NAME));

    // Try to do a transaction across multiple master failures
    c.set_credentials(NAME, NAME);
    EXPECT(c.connect());

    cout << "Start transaction, insert a value and read it" << endl;
    EXPECT(c.query("START TRANSACTION"));
    EXPECT(c.query("INSERT INTO test.t1 VALUES (1)"));
    EXPECT(c.query("SELECT * FROM test.t1 WHERE id = 1"));

    cout << "Killing connection" << endl;
    EXPECT(master.query("KILL CONNECTION USER " + NAME));

    cout << "Insert value and read it" << endl;
    EXPECT(c.query("INSERT INTO test.t1 VALUES (2)"));
    EXPECT(c.query("SELECT * FROM test.t1 WHERE id = 2"));

    cout << "Killing second connection" << endl;
    EXPECT(master.query("KILL CONNECTION USER " + NAME));

    cout << "Inserting value 3" << endl;
    EXPECT(c.query("INSERT INTO test.t1 VALUES (3)"));
    EXPECT(c.query("SELECT * FROM test.t1 WHERE id = 3"));

    cout << "Killing third connection" << endl;
    EXPECT(master.query("KILL CONNECTION USER " + NAME));

    cout << "Selecting final result" << endl;
    EXPECT(c.query("SELECT SUM(id) FROM test.t1"));

    cout << "Killing fourth connection" << endl;
    EXPECT(master.query("KILL CONNECTION USER " + NAME));

    cout << "Committing transaction" << endl;
    EXPECT(c.query("COMMIT"));

    cout << "Checking results" << endl;
    EXPECT(c.connect());
    auto res = c.field("SELECT SUM(id), @@last_insert_id FROM t1");
    test.expect(res == "6", "All rows were not inserted: %s", res.c_str());

    EXPECT(master.query("DROP TABLE test.t1"));
    EXPECT(master.query("DROP USER " + NAME));

    return test.global_result;
}
