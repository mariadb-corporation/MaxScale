/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include "mariadbmon_utils.hh"

using std::string;

namespace
{
void test_main(TestConnections& test)
{
    auto& repl = *test.repl;
    auto& mxs = *test.maxscale;

    stress_test::check_semisync_off(test);

    if (test.ok())
    {
        testclient::Settings client_sett;
        client_sett.host = mxs.ip4();
        client_sett.port = mxs.rwsplit_port;
        client_sett.user = mxs.user_name();
        client_sett.pw = mxs.password();
        client_sett.rows = 100;

        // The old master may diverge, so can only assume three failovers.
        test.tprintf("Running with normal replication.");
        stress_test::BaseSettings fail_sett;
        fail_sett.test_duration = 30;
        fail_sett.test_clients = 4;
        fail_sett.min_expected_failovers = 3;
        fail_sett.diverging_allowed = true;
        stress_test::run_failover_stress_test(test, fail_sett, client_sett);
    }
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}
