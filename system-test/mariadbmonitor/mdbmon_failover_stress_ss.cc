/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
#include "mariadbmon_utils.hh"

using std::string;
using semisync::check_semisync_status;

namespace
{
auto master = mxt::ServerInfo::master_st;
auto slave = mxt::ServerInfo::slave_st;
auto down = mxt::ServerInfo::DOWN;

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;

    // Check semisync is off when starting.
    semisync::check_semisync_off(test);
    create_test_user(test);

    if (test.ok())
    {
        test.tprintf("Setting up semisync replication.");
        semisync::setup_semisync_replication(test);

        if (test.ok())
        {
            test.tprintf("Running stress test with semisync replication.");

            testclient::Settings client_sett;
            client_sett.host = mxs.ip4();
            client_sett.port = mxs.rwsplit_port;
            client_sett.user = test_user_un();
            client_sett.pw = test_user_pw();
            client_sett.rows = 100;

            // Semisync replication slows down the test, so the rate of failovers is rather low.
            stress_test::BaseSettings fail_sett;
            fail_sett.test_duration = 60;
            fail_sett.test_clients = 4;
            fail_sett.min_expected_failovers = 5;
            // Using init-rpl-rol=SLAVE, so old master should not diverge.
            fail_sett.diverging_allowed = false;

            stress_test::run_failover_stress_test(test, fail_sett, client_sett);
        }


        test.tprintf("Restoring normal replication.");
        semisync::restore_normal_replication(test);
    }
    mxs.wait_for_monitor();
    drop_test_user(test);
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}
