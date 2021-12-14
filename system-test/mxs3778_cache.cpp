/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <string>
#include <maxtest/testconnections.hh>

using namespace std;

namespace
{

void query(TestConnections& t, Connection& c, const string& stmt)
{
    if (!c.query(stmt))
    {
        string s = "Could not execute: '" + stmt + "'";

        t.expect(false, "%s", s.c_str());
    }
}

void setup(TestConnections& test, Connection& c, const string& table)
{
    query(test, c, "DROP TABLE IF EXISTS " + table);
    query(test, c, "CREATE TABLE " + table + " (f INT)");
    query(test, c, "INSERT INTO " + table + " VALUES (1)");
}

}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    Connection c = test.maxscale->rwsplit();

    test.expect(c.connect(), "Could not connect to rwsplit.");

    setup(test, c, "test.mxs3778_t1");
    setup(test, c, "test.mxs3778_t2");

    // This SELECT results in two invalidation words for the resultset: test.mxs3778_t1 and test.mxs3778_t2.
    query(test, c, "SELECT * FROM test.mxs3778_t1 UNION SELECT * FROM test.mxs3778_t2");

    // This will cause the entry to be invalidated. The bookkeeping should be removed from
    // test.mxs3778_t1 AND test.mxs3778_t2.
    query(test, c, "DELETE FROM test.mxs3778_t1");

    // Unless the bookkeeping was updated properly, this will now cause a crash.
    query(test, c, "DELETE FROM test.mxs3778_t2");

    // Cleanup
    c.query("DROP TABLE test.mxs3778_t1");
    c.query("DROP TABLE test.mxs3778_t2");

    return 0;
}
