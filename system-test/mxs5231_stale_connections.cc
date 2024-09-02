/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-09-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

void test_mxs5231(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    c.connect();
    std::set<std::string> ids_before;

    // With no other load, the load balancing should spread the reads across all servers.
    for (int i = 0; i < 10; i++)
    {
        ids_before.insert(c.field("SELECT @@server_id, SLEEP(0.1)"));
    }

    test.expect(ids_before.size() == 3, "Expected 3 servers to be used for reads: %lu", ids_before.size());

    test.check_maxctrl("stop monitor MariaDB-Monitor");
    test.check_maxctrl("set server server3 maintenance");

    std::set<std::string> ids_after;

    // The reads should now be redirected to the remaining two servers.
    for (int i = 0; i < 10; i++)
    {
        ids_after.insert(c.field("SELECT @@server_id"));
    }

    test.expect(ids_after.size() == 2, "Expected 2 servers to be used for reads: %lu", ids_after.size());

    auto num_conn = test.maxctrl("api get servers/server3 data.attributes.statistics.connections").output;
    test.expect(num_conn == "0", "Expected no connections on server3, found: %s", num_conn.c_str());
}

int main(int argc, char** argv)
{
    TestConnections().run_test(argc, argv, test_mxs5231);
}
