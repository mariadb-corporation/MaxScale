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

    string stmt("CREATE TABLE cache_test (a INT)");

    cout << stmt << endl;
    test.try_query(pMysql, "%s", stmt.c_str());
}

void insert(TestConnections& test)
{
    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    string stmt("INSERT INTO cache_test VALUES (1)");

    cout << stmt << endl;
    test.try_query(pMysql, "%s", stmt.c_str());
}

void update(TestConnections& test, int value)
{
    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    string stmt("UPDATE cache_test SET a=");
    stmt += std::to_string(value);

    cout << stmt << endl;
    test.try_query(pMysql, "%s", stmt.c_str());
}

void select(TestConnections& test, int* pValue)
{
    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    string stmt("SELECT * FROM cache_test");

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

            test.assert(nRows == 1, "Unexpected number of rows: %u", nRows);
        }
    }
    else
    {
        test.assert(false, "SELECT failed.");
    }
}

namespace Cache
{

enum What
{
    POPULATE,
    USE
};

}

void set(TestConnections& test, Cache::What what, bool value)
{
    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    string stmt("SET @maxscale.cache.");
    stmt += ((what == Cache::POPULATE) ? "populate" : "use");
    stmt += "=";
    stmt += (value ? "true" : "false");

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

    // Let's populate the cache.
    set(test, Cache::POPULATE, true);
    set(test, Cache::USE, false);
    select(test, &value);
    test.assert(value == 1, "Initial value was not 1.");

    // And update the real value.
    update(test, 2); // Now the cache contains 1 and the db 2.

    // With @maxscale.cache.use==false we should get the updated value.
    set(test, Cache::POPULATE, false);
    set(test, Cache::USE, false);
    select(test, &value);
    test.assert(value == 2, "The value received was not the latest one.");

    // With @maxscale.cache.use==true we should get the old one, since
    // it was not updated above as @maxscale.cache.populate==false.
    set(test, Cache::POPULATE, false);
    set(test, Cache::USE, true);
    select(test, &value);
    test.assert(value == 1, "The value received was not the populated one.");

    // The hard_ttl is 8, so we sleep(10) seconds to ensure that TTL has passed.
    cout << "Sleeping 10 seconds." << endl;
    sleep(10);

    // With @maxscale.cache.use==true we should now get the latest value.
    // The value in the cache is stale, so it will be updated even if
    // @maxscale.cache.populate==false.
    set(test, Cache::POPULATE, false);
    set(test, Cache::USE, true);
    select(test, &value);
    test.assert(value == 2, "The cache was not updated even if TTL was passed.");

    // Let's update again
    update(test, 3);

    // And fetch again. Should still be 2, as the item in the cache is not stale.
    set(test, Cache::POPULATE, false);
    set(test, Cache::USE, true);
    select(test, &value);
    test.assert(value == 2, "New value %d, although the value in the cache is not stale.", value);

    // Force an update.
    set(test, Cache::POPULATE, true);
    set(test, Cache::USE, false);
    select(test, &value);
    test.assert(value == 3, "Did not get new value.");

    // And check that the cache indeed was updated, but update the DB first.
    update(test, 4);

    set(test, Cache::POPULATE, false);
    set(test, Cache::USE, true);
    select(test, &value);
    test.assert(value == 3, "Got a newer value than expected.");
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

    return test.global_result;
}
