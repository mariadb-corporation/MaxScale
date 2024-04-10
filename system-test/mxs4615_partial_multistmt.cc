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

/**
 * MXS-4615: Partially executed multistatements aren't treated as partial results
 */
#include <maxtest/testconnections.hh>

void test_main(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());

    std::thread thr([&](){
        test.expect(c.query("BEGIN NOT ATOMIC SELECT 1; SELECT SLEEP(5); SELECT 2; END"),
                    "Query should fail: %s", c.error());
    });

    sleep(2);

    // Block and unblock the master
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor(2);
    test.repl->unblock_node(0);
    test.maxscale->wait_for_monitor(2);

    thr.join();
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
