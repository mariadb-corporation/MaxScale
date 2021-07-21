/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <string>
#include <vector>
#include <maxtest/testconnections.hh>

using namespace std;

namespace
{

const char* zUser = "maxuser";
const char* zPwd  = "maxuser";

void drop(TestConnections& test)
{
    MYSQL* pMysql = test.maxscale->conn_rwsplit[0];

    test.try_query(pMysql, "DROP TABLE IF EXISTS cache_users");
}

void create(TestConnections& test)
{
    drop(test);

    MYSQL* pMysql = test.maxscale->conn_rwsplit[0];

    test.try_query(pMysql, "CREATE TABLE cache_users (f INT)");
}

void check(TestConnections& test, Connection& c1, Connection& c2, const string& stmt)
{
    c1.query(stmt);

    Result rows1 = c1.rows("SELECT * FROM cache_users");
    Result rows2 = c2.rows("SELECT * FROM cache_users");

    test.expect(rows1 == rows2,
                "After '%s' the result result was not identical for different users.",
                stmt.c_str());
}

void run(TestConnections& test)
{
    create(test);

    Connection c1 = test.maxscale->rwsplit();
    c1.connect();
    Connection c2 = test.maxscale->rwsplit();
    c2.set_credentials(zUser, zPwd);
    c2.connect();

    c1.query("INSERT INTO cache_users values (1)");

    Result rows1 = c1.rows("SELECT * FROM cache_users");
    Result rows2 = c2.rows("SELECT * FROM cache_users");

    test.expect(rows1 == rows2, "Initial rows were not identical.");

    check(test, c1, c2, "INSERT INTO cache_users values (2)");
    check(test, c1, c2, "UPDATE cache_users SET f = 3 WHERE f = 2");
    check(test, c1, c2, "DELETE FROM cache_users WHERE f = 3");

    drop(test);
}
}

// No matter whether users=mixed or users=isolated is used, an
// invalidation caused by one user should immediately be visible
// by all other users if data=shared is used.

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto maxscales = test.maxscale;

    if (maxscales->connect_rwsplit() == 0)
    {
        test.tprintf("Testing users=mixed.");
        run(test);

        maxscales->ssh_node(
            "sed -i \"s/users=mixed/users=isolated/\" /etc/maxscale.cnf",
            true);
        maxscales->restart_maxscale();

        // To be certain that MaxScale has started.
        sleep(3);

        if (maxscales->connect_rwsplit() == 0)
        {
            test.tprintf("Testing users=isolated.");
            run(test);

            drop(test);
        }
        else
        {
            test.expect(false, "Could not re-connect to rwsplit.");
        }
    }

    return test.global_result;
}
