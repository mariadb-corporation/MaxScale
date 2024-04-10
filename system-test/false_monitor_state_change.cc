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
 * Test false server state changes when manually clearing master bit
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("Block master");
    test.repl->block_node(0);

    test.tprintf("Wait for monitor to see it");
    test.maxscale->wait_for_monitor();

    test.tprintf("Clear master status");
    test.maxctrl("clear server server1 master");
    test.maxscale->wait_for_monitor();

    test.repl->unblock_node(0);
    test.maxscale->wait_for_monitor();

    return test.global_result;
}
