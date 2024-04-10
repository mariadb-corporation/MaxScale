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

#include <maxtest/testconnections.hh>
#include "mariadbmon_utils.hh"

using std::string;
using stress_test::check_semisync_status;

namespace
{
void test_main(TestConnections& test)
{
    auto& repl = *test.repl;
    auto& mxs = *test.maxscale;

    // Check semisync is off when starting.
    stress_test::check_semisync_off(test);

    if (test.ok())
    {
        testclient::Settings client_sett;
        client_sett.host = mxs.ip4();
        client_sett.port = mxs.rwsplit_port;
        client_sett.user = mxs.user_name();
        client_sett.pw = mxs.password();
        client_sett.rows = 100;

        // Semisync replication slows down the test, so the rate of failovers is rather low.
        stress_test::BaseSettings fail_sett;
        fail_sett.test_duration = 60;
        fail_sett.test_clients = 4;
        fail_sett.min_expected_failovers = 5;
        fail_sett.diverging_allowed = false;

        // Setup semisync replication. During the test, the master should not diverge.
        // Write the config values to config files so that they persist between restarts.
        test.tprintf("Setting up semisync replication.");
        repl.stop_nodes();
        for (int i = 0; i < repl.N; i++)
        {
            repl.stash_server_settings(i);
            repl.add_server_setting(i, "rpl_semi_sync_master_enabled=ON");
            repl.add_server_setting(i, "rpl_semi_sync_slave_enabled=ON");
            repl.start_node(i);
        }
        sleep(1);

        check_semisync_status(test, 1, true, true, 0);
        check_semisync_status(test, 2, true, true, 0);
        check_semisync_status(test, 3, true, true, 0);
        // The following should be
        // check_semisync_status(test, 0, true, false, 3);
        // Change it back once server reports correct status.
        check_semisync_status(test, 0, true, true, 3);

        if (test.ok())
        {
            test.tprintf("Running stress test with semisync replication.");
            stress_test::run_failover_stress_test(test, fail_sett, client_sett);
        }

        test.tprintf("Restoring normal replication.");
        repl.stop_nodes();
        for (int i = 0; i < repl.N; i++)
        {
            repl.restore_server_settings(i);
        }
        repl.start_nodes();

        stress_test::check_semisync_off(test);
    }
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}
