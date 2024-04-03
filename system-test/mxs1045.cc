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
 * @file mxs1045.cpp Regression case for the bug "Defunct processes after maxscale have executed script during
 * failover"
 * - configure monitor:
 * @verbatim
 *  script=/bin/sh -c "echo hello world!"
 *  events=master_down,server_down
 *
 * @endverbatim
 * - block one node
 * - Check that script execution doesn't leave zombie processes
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("Block master");
    test.repl->block_node(0);

    test.tprintf("Wait for monitor to see it");
    test.maxscale->wait_for_monitor();

    test.tprintf("Check that there are no zombies");

    int res = test.maxscale->ssh_node(
        "if [ \"`ps -ef|grep defunct|grep -v grep`\" != \"\" ]; then exit 1; fi",
        false);
    test.add_result(res, "Zombie processes were found");

    test.repl->unblock_node(0);

    return test.global_result;
}
