/*
 * Copyright (c) 2024 MariaDB plc
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

#include <maxtest/testconnections.hh>

void test_mxs5085(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    c.connect();
    auto first_id = c.field("SELECT @@server_id");
    test.check_maxctrl("call command mariadbmon switchover MariaDB-Monitor");
    test.maxscale->wait_for_monitor();
    auto second_id = c.field("SELECT @@server_id");
    test.expect(first_id != second_id, "Query should not be routed to the same server after switchover");
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_mxs5085);
}
