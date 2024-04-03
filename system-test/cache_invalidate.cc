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

#include <iostream>
#include <string>
#include <vector>
#include <maxtest/testconnections.hh>

using namespace std;

namespace
{

const int PORT_LOCAL_CACHE = 4006;
const int PORT_REDIS_CACHE = 4007;

const char* ZCREATE_STMT = "CREATE TABLE cache_invalidate (f INT)";
const char* ZDROP_STMT   = "DROP TABLE IF EXISTS cache_invalidate";
const char* ZSELECT_STMT = "SELECT * FROM cache_invalidate";

void drop(TestConnections& test)
{
    MYSQL* pMysql = test.maxscale->conn_rwsplit;

    test.try_query(pMysql, "%s", ZDROP_STMT);
}

void create(TestConnections& test)
{
    drop(test);

    MYSQL* pMysql = test.maxscale->conn_rwsplit;

    test.try_query(pMysql, "%s", ZCREATE_STMT);
}

Result prepare(TestConnections& test, Connection& c)
{
    drop(test);
    create(test);
    c.query("INSERT INTO cache_invalidate values (2)");

    return c.rows(ZSELECT_STMT);
}

enum class Expect
{
    SAME,
    DIFFERENT
};

Result check(TestConnections& test, Connection& c, const string& stmt, Expect expect, const Result& base_line,
             const char* zQuery = "SELECT * FROM cache_invalidate")
{
    c.query(stmt);
    Result rows = c.rows(zQuery);

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

void run(TestConnections& test, int port, Expect expect)
{
    create(test);

    Connection c = test.maxscale->get_connection(port);
    c.connect();
    if (port == PORT_REDIS_CACHE)
    {
        // Short sleep, so that the asynchronous connecting surely
        // has time to finish.
        sleep(1);
    }
    c.query("INSERT INTO cache_invalidate values (1)");

    Result rows = c.rows("SELECT * FROM cache_invalidate");

    // Straightforward cases.
    rows = check(test, c, "INSERT INTO cache_invalidate values (2)", expect, rows);
    rows = check(test, c, "UPDATE cache_invalidate SET f = 3 WHERE f = 2", expect, rows);
    rows = check(test, c, "DELETE FROM cache_invalidate WHERE f = 3", expect, rows);

    // Esoteric cases

    // ALTER
    rows = prepare(test, c);

    check(test, c, "ALTER TABLE cache_invalidate RENAME cache_invalidate_2", expect, rows);
    c.query("ALTER TABLE cache_invalidate_2 RENAME cache_invalidate");

    // DROP
    rows = prepare(test, c);

    check(test, c, ZDROP_STMT, expect, rows);

    // RENAME
    rows = prepare(test, c);

    check(test, c, "RENAME TABLE cache_invalidate TO cache_invalidate_2", expect, rows);
    c.query("RENAME TABLE cache_invalidate_2 TO cache_invalidate");

    drop(test);

    // information_schema
    rows = c.rows("SELECT table_name FROM information_schema.tables ORDER BY table_name");
    rows = check(test, c, "CREATE TABLE cache_invalidate_create (f INT)", expect, rows,
                 "SELECT table_name FROM information_schema.tables ORDER BY table_name");

    check(test, c, "DROP TABLE cache_invalidate_create", expect, rows,
          "SELECT table_name FROM information_schema.tables ORDER BY table_name");
}
}

void install_and_start_redis(mxt::MaxScale& maxscales)
{
    setenv("maxscale_000_keyfile", maxscales.sshkey(), 0);
    setenv("maxscale_000_whoami", maxscales.access_user(), 0);
    setenv("maxscale_000_network", maxscales.ip4(), 0);

    // This will install memcached as well, but that's ok.

    string path(mxt::SOURCE_DIR);
    path += "/cache_install_and_start_storages.sh";

    system(path.c_str());
}

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    auto maxscales = test.maxscale;

    install_and_start_redis(*maxscales);

    maxscales->start();

    if (maxscales->connect_rwsplit() == 0)
    {
        // Non-invalidated cache
        test.tprintf("Testing non-invalidated cache.");
        test.tprintf("Local storage.");
        run(test, PORT_LOCAL_CACHE, Expect::SAME);
        test.tprintf("Redis storage.");
        run(test, PORT_REDIS_CACHE, Expect::SAME);

        // Invalidated cache

        // When the 'invalidate' flag is turned on, we also need to flush redis.
        // Otherwise there will be entries that are not subject to invalidation.
        maxscales->ssh_node(
            "sed -i \"s/invalidate=never/invalidate=current/\" /etc/maxscale.cnf; "
            "redis-cli flushall",
            true);
        maxscales->restart_maxscale();

        // To be certain that MaxScale has started.
        sleep(3);

        if (maxscales->connect_rwsplit() == 0)
        {
            test.tprintf("Testing invalidated cache.");
            test.tprintf("Local storage.");
            run(test, PORT_LOCAL_CACHE, Expect::DIFFERENT);
            test.tprintf("Redis storage.");
            run(test, PORT_REDIS_CACHE, Expect::DIFFERENT);
        }
        else
        {
            test.expect(false, "Could not connect to rwsplit.");
        }
    }

    return test.global_result;
}
