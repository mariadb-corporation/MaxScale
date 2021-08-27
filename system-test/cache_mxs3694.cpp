/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <string>
#include <thread>
#include <maxtest/mariadb_connector.hh>
#include <maxtest/testconnections.hh>

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto sRws1 = test.maxscale->open_rwsplit_connection2();
    auto sRws2 = test.maxscale->open_rwsplit_connection2();

    if (sRws1->cmd("CREATE TABLE IF NOT EXISTS mxs3694 (f INT)"))
    {
        // Populate cache.
        sRws1->cmd("INSERT INTO mxs3694 VALUES (42)");
        sRws1->query("SELECT * FROM mxs3694");

        thread t([&sRws2]() {
                // This update will invalidate the cached items, but will
                // return only after a SELECT is performed when the hard
                // TTL has passed.
                sRws2->cmd("UPDATE mxs3694 SET f = f + SLEEP(8)");
            });

        // Hard TTL is 4 seconds, so we sleep 5.
        sleep(5);
        // This will now cause the entry in the cache to be deleted.
        // Once the UPDATE above returns, there will be a crash unless
        // MXS-3694 has been fixed.
        sRws1->query("SELECT * FROM mxs3694");

        t.join();

        sRws1->cmd("DROP TABLE mxs3694");
    }
    else
    {
        test.expect(false, "Could not create database.");
    }

    return test.global_result;
}
