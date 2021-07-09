/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <string>
#include <vector>
#include <maxbase/string.hh>
#include <maxtest/testconnections.hh>

// This test checks
// - that a failure to connect to redis/memcached does not stall the client and
// - that when redis/memcached become available, they are transparently taken into use.

using namespace std;

namespace
{

const int PORT_RWS           = 4006;
const int PORT_RWS_REDIS     = 4007;
const int PORT_RWS_MEMCACHED = 4008;

const int TIMEOUT = 10; // This should be bigger that the cache timeout in the config.

bool restart_service(TestConnections& test, const char* zService)
{
    bool rv = test.maxscale->ssh_node_f(true, "service %s restart", zService) == 0;
    sleep(1); // A short sleep to ensure connecting is possible.
    return rv;
}

bool start_service(TestConnections& test, const char* zService)
{
    bool rv = test.maxscale->ssh_node_f(true, "service %s start", zService) == 0;
    sleep(1); // A short sleep to ensure connecting is possible.
    return rv;
}

bool stop_service(TestConnections& test, const char* zService)
{
    return test.maxscale->ssh_node_f(true, "service %s stop", zService) == 0;
}

bool start_redis(TestConnections& test)
{
    return start_service(test, "redis");
}

bool stop_redis(TestConnections& test)
{
    return stop_service(test, "redis");
}

bool start_memcached(TestConnections& test)
{
    return start_service(test, "memcached");
}

bool stop_memcached(TestConnections& test)
{
    return stop_service(test, "memcached");
}

void drop(TestConnections& test)
{
    MYSQL* pMysql = test.maxscale->conn_rwsplit[0];

    test.try_query(pMysql, "DROP TABLE IF EXISTS cache_distributed");

    test.maxscale->ssh_node_f(true, "redis-cli flushall");
    restart_service(test, "memcached");
}

void create(TestConnections& test)
{
    drop(test);

    MYSQL* pMysql = test.maxscale->conn_rwsplit[0];

    test.try_query(pMysql, "CREATE TABLE cache_distributed (f INT)");
}

Connection connect(TestConnections& test, int port)
{
    Connection c = test.maxscale->get_connection(port);
    bool connected = c.connect();

    test.expect(connected, "Could not connect to %d.", port);

    return c;
}

void insert(TestConnections& test, Connection& c)
{
    bool inserted = c.query("INSERT INTO cache_distributed values (1)");

    test.expect(inserted, "Could not insert value.");
}

void select(TestConnections& test, const char* zName, Connection& c, size_t n)
{
    Result rows = c.rows("SELECT * FROM cache_distributed");

    test.expect(rows.size() == n, "%s: Expected %lu rows, but got %lu.", zName, n, rows.size());
}

void install_and_start_redis_and_memcached(mxt::MaxScale& maxscales)
{
    setenv("maxscale_000_keyfile", maxscales.sshkey(), 0);
    setenv("maxscale_000_whoami", maxscales.access_user(), 0);
    setenv("maxscale_000_network", maxscales.ip4(), 0);

    string path(mxt::SOURCE_DIR);
    path += "/cache_install_and_start_storages.sh";

    system(path.c_str());
}

}

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    auto maxscales = test.maxscale;

    install_and_start_redis_and_memcached(*maxscales);

    maxscales->start();

    if (maxscales->connect_rwsplit() == 0)
    {
        create(test);
        sleep(1);

        Connection none = connect(test, PORT_RWS);
        insert(test, none);

        test.tprintf("Connecting with running redis/memcached.");

        test.reset_timeout();
        Connection redis = connect(test, PORT_RWS_REDIS);
        Connection memcached = connect(test, PORT_RWS_MEMCACHED);

        // There has been 1 insert so we should get 1 in all cases. As redis and memcached
        // are running, the caches will be populated as well.
        test.reset_timeout();
        select(test, "none", none, 1);
        select(test, "redis", redis, 1);
        select(test, "memcached", memcached, 1);

        test.tprintf("Stopping redis/memcached.");
        stop_redis(test);
        stop_memcached(test);

        test.tprintf("Connecting with stopped redis/memcached.");

        // Using a short timeout at connect-time ensure that if the async connecting
        // does not work, we'll get a quick failure.
        test.reset_timeout();
        redis = connect(test, PORT_RWS_REDIS);
        memcached = connect(test, PORT_RWS_MEMCACHED);

        // There has still been only one insert, so in all cases we should get just one row.
        // As redis and memcached are not running, the result comes from the backend.
        test.reset_timeout();
        select(test, "none", none, 1);
        select(test, "redis", redis, 1);
        select(test, "memcached", memcached, 1);

        // Lets add another row.
        insert(test, none);

        // There has been two inserts, and as redis/memcached are stopped, we should
        // get two in all cases.
        test.reset_timeout();
        select(test, "none", none, 2);
        select(test, "redis", redis, 2);
        select(test, "memcached", memcached, 2);

        test.tprintf("Starting redis/memcached.");
        start_redis(test);
        start_memcached(test);
        sleep(1); // To allow things to stabalize.

        // As the caches are now running, they will now be taken into use. However, that
        // will be triggered by the fetching and hence the first result will be fetched from
        // the backend and possibly cached as well, if the connection to the cache is established
        // faster that what getting the result from the backend is.
        test.reset_timeout();
        select(test, "none", none, 2);
        select(test, "redis", redis, 2);
        select(test, "memcache", memcached, 2);

        // To make sure the result ends up in the cache, we select again after having slept
        // for a short while.
        sleep(2);
        select(test, "redis", redis, 2);
        select(test, "memcached", memcached, 2);

        // Add another row, should not be visible from cached alternatives.
        insert(test, none);
        select(test, "none", none, 3);
        select(test, "redis", redis, 2);
        select(test, "memcached", memcached, 2);

        // Add yet another row, should not be visible from cached alternatives.
        insert(test, none);
        select(test, "none", none, 4);
        select(test, "redis", redis, 2);
        select(test, "memcached", memcached, 2);
    }
    else
    {
        ++test.global_result;
    }

    return test.global_result;
}
