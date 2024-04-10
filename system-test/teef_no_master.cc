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
 * @file bug664.cpp Tee filter branch session failure test
 *
 * - Configure MaxScale so that the branched session will always fail
 * - Execute query on the main service and check that MaxScale is alive
 * - An error should be logged about the failed branch session
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.check_maxscale_alive();
    test.log_includes("Failed to create new router session for service 'RW_Split'");
    return test.global_result;
}
