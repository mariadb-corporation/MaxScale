/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
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

const int PORT_RWS_REDIS = 4006;

void test_that_connecting_fails(TestConnections& test)
{
    test.tprintf("Testing that connecting fails.");

    Connection c = test.maxscale->get_connection(PORT_RWS_REDIS);
    test.expect(c.connect(), "1: Could not connect to MaxScale.");

    c.query("SELECT 1"); // Trigger connecting to Redis
    sleep(1);
    c.query("SELECT 1");
    sleep(1);

    test.log_includes("NOAUTH Authentication required");
}

void test_that_connecting_succeeds(TestConnections& test)
{
    test.tprintf("Testing that connecting succeeds.");

    Connection c = test.maxscale->get_connection(PORT_RWS_REDIS);
    test.expect(c.connect(), "1: Could not connect to MaxScale.");

    c.query("SELECT 1"); // Trigger connecting to Redis
    sleep(1);
    c.query("SELECT 1");
    sleep(1);

    test.log_includes("Redis authentication succeeded");
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

    // Make redis require a password
    maxscales->ssh_node(
        "sed -i \"s/# requirepass foobared/requirepass foobared/\" /etc/redis.conf; "
        "systemctl restart redis",
        true);

    maxscales->start();
    sleep(1);

    test_that_connecting_fails(test);

    // Make MaxScale provide a password
    maxscales->ssh_node(
        "sed -i \"s/server=127.0.0.1/server=127.0.0.1,password=foobared/\" /etc/maxscale.cnf; "
        "systemctl restart maxscale",
        true);

    sleep(1);

    test_that_connecting_succeeds(test);

    // Remove redis requirement of a password
    maxscales->ssh_node(
        "sed -i \"s/requirepass foobared/# requirepass foobared/\" /etc/redis.conf; "
        "systemctl restart redis",
        true);

    return test.global_result;
}
