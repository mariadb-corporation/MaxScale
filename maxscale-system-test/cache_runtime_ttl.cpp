/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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

enum class Column
{
    A,
    B,
};

void drop(TestConnections& test)
{
    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    string stmt("DROP TABLE IF EXISTS cache_test");

    cout << stmt << endl;
    test.try_query(pMysql, "%s", stmt.c_str());
}

void create(TestConnections& test)
{
    drop(test);

    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    string stmt("CREATE TABLE cache_test (a INT, b INT)");

    cout << stmt << endl;
    test.try_query(pMysql, "%s", stmt.c_str());
}

void insert(TestConnections& test)
{
    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    string stmt("INSERT INTO cache_test VALUES (1, 1)");

    cout << stmt << endl;
    test.try_query(pMysql, "%s", stmt.c_str());
}

void update(TestConnections& test, Column column, int value)
{
    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    string stmt("UPDATE cache_test SET ");
    stmt += (column == Column::A) ? "a=" : "b=";
    stmt += std::to_string(value);

    cout << stmt << endl;
    test.try_query(pMysql, "%s", stmt.c_str());
}

void select(TestConnections& test, Column column, int* pValue)
{
    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    string stmt("SELECT ");
    stmt += (column == Column::A) ? "a" : "b";
    stmt += " FROM cache_test";

    cout << stmt << endl;

    if (mysql_query(pMysql, stmt.c_str()) == 0)
    {
        if (mysql_field_count(pMysql) != 0)
        {
            size_t nRows = 0;

            do
            {
                MYSQL_RES* pRes = mysql_store_result(pMysql);
                MYSQL_ROW row = mysql_fetch_row(pRes);
                *pValue = atoi(row[0]);
                mysql_free_result(pRes);
                ++nRows;
            }
            while (mysql_next_result(pMysql) == 0);

            test.expect(nRows == 1, "Unexpected number of rows: %u", nRows);
        }
    }
    else
    {
        test.expect(false, "SELECT failed.");
    }
}

namespace Cache
{

enum What
{
    SOFT_TTL,
    HARD_TTL
};
}

void set(TestConnections& test, Cache::What what, uint32_t value)
{
    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    string stmt("SET @maxscale.cache.");
    stmt += ((what == Cache::SOFT_TTL) ? "soft_ttl" : "hard_ttl");
    stmt += "=";
    stmt += std::to_string(value);

    cout << stmt << endl;
    test.try_query(pMysql, "%s", stmt.c_str());
}
}


namespace
{

void init(TestConnections& test)
{
    create(test);
    insert(test);
}

void run(TestConnections& test)
{
    init(test);
    int value;

    // Let's set a long hard_ttl so that it will not interfere
    // with the soft_ttl testing.
    set(test, Cache::HARD_TTL, 60);
    // And update the cache.
    select(test, Column::A, &value);
    test.expect(value == 1, "Initial value was not 1.");
    select(test, Column::B, &value);
    test.expect(value == 1, "Initial value was not 1.");

    // Update the real value.
    update(test, Column::A, 2);     // Now the cache contains 1 and the db 2.
    update(test, Column::B, 2);     // Now the cache contains 1 and the db 2.

    sleep(5);

    // With a soft_ttl less that the amount we slept should mean that
    // the value in the cache is considered stale and that the value
    // is fetched from the server.
    set(test, Cache::SOFT_TTL, 4);
    select(test, Column::A, &value);
    test.expect(value == 2, "The value received was not the latest one.");

    // With a soft_ttl larger that the amount we slept should mean that
    // the value in the cache is *not* considered stale and that we
    // get that value.
    set(test, Cache::SOFT_TTL, 10);
    select(test, Column::B, &value);
    test.expect(value == 1, "The value received was not from the cache.");
}
}

int main(int argc, char* argv[])
{
    std::ios::sync_with_stdio(true);

    TestConnections test(argc, argv);

    if (test.maxscales->connect_rwsplit() == 0)
    {
        run(test);
    }

    test.maxscales->connect();
    drop(test);
    test.maxscales->disconnect();

    return test.global_result;
}
