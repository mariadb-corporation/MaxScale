/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

void test_main(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();

    auto block_and_select = [&](bool expected){
        test.repl->block_node(0);
        test.maxscale->wait_for_monitor(2);
        test.repl->unblock_node(0);
        test.maxscale->wait_for_monitor(2);

        test.expect(c.query("SELECT 1") == expected, "SELECT %s: %s",
                    expected ? "failed" : "succeeded", c.error());
    };

    test.tprintf("Creating and then dropping a temporary table should not close the connection.");

    test.expect(c.connect(), "Connection failed: %s", c.error());
    test.expect(c.query("CREATE TEMPORARY TABLE t1(id INT)"), "CREATE failed: %s", c.error());
    test.expect(c.query("DROP TABLE t1"), "DROP failed: %s", c.error());
    block_and_select(true);

    test.tprintf("Losing a connection when a temporary table exists should close the connection.");

    test.expect(c.connect(), "Connection failed: %s", c.error());
    test.expect(c.query("CREATE TEMPORARY TABLE t1(id INT)"), "CREATE failed: %s", c.error());
    block_and_select(false);

    test.tprintf("strict_tmp_tables=false should ignore lost temporary tables.");

    test.maxctrl("alter service RW-Split-Router strict_tmp_tables=false");
    test.expect(c.connect(), "Connection failed: %s", c.error());
    test.expect(c.query("CREATE TEMPORARY TABLE t1(id INT)"), "CREATE failed: %s", c.error());
    block_and_select(true);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
