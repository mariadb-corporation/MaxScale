/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/string.hh>

void test_mxs5085(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    c.connect();
    auto first_id = c.field("SELECT @@server_id");
    test.check_maxctrl("call command mariadbmon switchover MariaDB-Monitor");
    test.maxscale->wait_for_monitor();
    auto second_id = c.field("SELECT @@server_id");
    test.expect(first_id != second_id, "Query should not be routed to the same server after switchover");
    test.check_maxctrl("call command mariadbmon reset-replication MariaDB-Monitor server1");
}

void test_mxs5209(TestConnections& test)
{
    test.check_maxctrl("stop monitor MariaDB-Monitor");

    auto c = test.maxscale->rwsplit();
    c.connect();
    auto first_id = c.row("SELECT @@hostname, @@server_id");

    test.check_maxctrl("clear server server1 master");
    test.check_maxctrl("set server server1 slave");
    test.check_maxctrl("clear server server2 slave");
    test.check_maxctrl("set server server2 master");

    auto second_id = c.row("SELECT @@hostname, @@server_id");
    test.log_includes("Replacing old master 'server1' with new master 'server2'");
    test.expect(first_id != second_id,
                "Query should not be routed to the same server after master changes: %s",
                mxb::join(first_id).c_str());

    test.check_maxctrl("start monitor MariaDB-Monitor");
}

void test_main(TestConnections& test)
{
    test.log_printf("MXS-5085: Readwritesplit creates a slave connection after a switchover");
    test_mxs5085(test);

    test.log_printf("MXS-5209: Readwritesplit does not discard stale connections if the master has changed");
    test_mxs5209(test);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
