/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Check if Maxscale priocess is running as 'maxscale'
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.reset_timeout();
    auto res = test.maxscale->ssh_output("ps -U maxscale -C maxscale -o user --no-headers").output;
    res = res.substr(0, strlen("maxscale"));
    test.expect(res == "maxscale", "MaxScale running as '%s' instead of 'maxscale'", res.c_str());

    return test.global_result;
}
