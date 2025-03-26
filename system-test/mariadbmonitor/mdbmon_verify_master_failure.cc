/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-1493: https://jira.mariadb.org/browse/MXS-1493
 *
 * Testing of master failure verification
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("Blocking master and checking that master failure is delayed at least once.");
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor();
    test.log_includes("If master does not return in .* monitor tick(s), failover begins.");

    test.tprintf("Waiting to see if failover is performed.");
    bool found = false;

    for (int i = 0; i < 15 && !found; i++)
    {
        sleep(1);
        test.maxscale->wait_for_monitor();
        found = test.log_matches("Performing.*failover");
    }

    test.expect(found, "Expected to find 'Performing.*failover' in the log.");

    // TODO: Extend the test

    return test.global_result;
}
