/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

/**
 * Regression case for crash if 'show monitors' command is issued, but no monitor is not running
 *
 * - maxscale.cnf contains wrong monitor config (user name is wrong)
 * - issue 'show monitors' command
 * - check for crash
 */

#include <maxtest/testconnections.hh>

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.check_maxctrl("show monitors");
    test.log_includes("Auth Error, Down");
    return test.global_result;
}
