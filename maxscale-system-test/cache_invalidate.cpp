/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <string>
#include <vector>
#include "testconnections.h"

using namespace std;

namespace
{

void drop(TestConnections& test)
{
    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    test.try_query(pMysql, "DROP TABLE IF EXISTS cache_invalidate");
}

void create(TestConnections& test)
{
    drop(test);

    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    test.try_query(pMysql, "CREATE TABLE cache_invalidate (f INT)");
}

enum class Expect
{
    SAME,
    DIFFERENT
};

Result check(TestConnections& test, Connection& c, const string& stmt, Expect expect, const Result& base_line)
{
    c.query(stmt);
    Result rows = c.rows("SELECT * FROM cache_invalidate");

    if (expect == Expect::SAME)
    {
        test.tprintf("Non-invalidated cache, so after '%s' the results should still be the same.",
                     stmt.c_str());

        test.expect(base_line == rows,
                    "After '%s' the result result was not identical from a non-invalidated cache.",
                    stmt.c_str());
    }
    else
    {
        test.tprintf("Invalidated cache, so after '%s' the results should be different.",
                     stmt.c_str());

        test.expect(base_line != rows,
                    "After '%s' the result result was identical from an invalidated cache.",
                    stmt.c_str());
    }

    return rows;
}

void run(TestConnections& test, Expect expect)
{
    create(test);

    Connection c = test.maxscales->rwsplit();
    c.connect();
    c.query("INSERT INTO cache_invalidate values (1)");

    Result rows = c.rows("SELECT * FROM cache_invalidate");

    rows = check(test, c, "INSERT INTO cache_invalidate values (2)", expect, rows);
    rows = check(test, c, "UPDATE cache_invalidate SET f = 3 WHERE f = 2", expect, rows);
    rows = check(test, c, "DELETE FROM cache_invalidate WHERE f = 3", expect, rows);

    drop(test);
}
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto maxscales = test.maxscales;

    if (maxscales->connect_rwsplit() == 0)
    {
        // Non-invalidated cache
        test.tprintf("Testing non-invalidated cache.");
        run(test, Expect::SAME);

        // Invalidated cache
        maxscales->ssh_node(0,
                            "sed -i \"s/invalidate=never/invalidate=current/\" /etc/maxscale.cnf",
                            true);
        maxscales->restart_maxscale();

        // To be certain that MaxScale has started.
        sleep(3);

        if (maxscales->connect_rwsplit(0) == 0)
        {
            test.tprintf("Testing invalidated cache.");
            run(test, Expect::DIFFERENT);

            drop(test);
        }
        else
        {
            test.expect(false, "Could not connect to rwsplit.");
        }
    }

    return test.global_result;
}
