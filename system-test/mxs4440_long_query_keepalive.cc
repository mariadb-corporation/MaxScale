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

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.repl->connect();
    auto master_id = test.repl->get_server_id_str(0);
    test.expect(master_id != "-1", "Failed to fetch @@server_id from node 0");

    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());
    test.expect(c.query("SET wait_timeout = 10"), "'SET wait_timeout' failed: %s", c.error());

    while (c.field("SELECT @@server_id") == master_id)
    {
        std::this_thread::sleep_for(50ms);
    }

    test.expect(c.query("SELECT SLEEP(30)"), "'SELECT SLEEP(30)' failed: %s", c.error());

    return test.global_result;
}
