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

#include <maxtest/testconnections.hh>
#include <iostream>
#include <string>

using std::string;
using std::cout;

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& log = test.logger();
    auto& repl = *test.repl;

    const int disk_check_wait = 3;      // Monitor checks disk info every 2s.

    // Enable the disks-plugin on all servers. Has to be done before MaxScale is on to prevent disk space
    // monitoring from disabling itself due to errors.
    bool disks_plugin_loaded = false;
    const char strict_mode[] = "SET GLOBAL gtid_strict_mode=%i;";
    repl.ping_or_open_admin_connections();
    for (int i = 0; i < repl.N; i++)
    {
        auto conn = repl.backend(i)->admin_connection();
        conn->cmd("INSTALL SONAME 'disks';");
        conn->cmd_f(strict_mode, 1);
    }

    if (test.ok())
    {
        test.tprintf("Disks-plugin installed and gtid_strict_mode enabled on all servers. "
                     "Starting MaxScale.");
        mxs.start_and_check_started();
        sleep(disk_check_wait);
        mxs.wait_for_monitor(1);
        disks_plugin_loaded = true;
    }
    else
    {
        test.tprintf("Test preparations failed.");
    }

    auto master = mxt::ServerInfo::master_st;
    auto slave = mxt::ServerInfo::slave_st;
    auto maint = mxt::ServerInfo::MAINT | mxt::ServerInfo::RUNNING;

    const char insert_query[] = "INSERT INTO test.t1 VALUES (%i);";
    int insert_val = 1;

    if (test.ok())
    {
        // Set up test table to ensure queries are going through.
        test.tprintf("Creating table and inserting data.");
        auto maxconn = test.maxscale->open_rwsplit_connection2();
        maxconn->cmd("CREATE OR REPLACE TABLE test.t1(c1 INT)");
        maxconn->cmd_f(insert_query, insert_val++);

        // server2 is always out of disk space.
        mxs.check_print_servers_status({master, maint, slave, slave});
    }

    if (test.ok())
    {
        // If ok so far, change the disk space threshold to something tiny to force a switchover.
        log.log_msg("Changing disk space threshold for the monitor, should cause a switchover.");
        mxs.maxctrl("alter monitor MySQL-Monitor disk_space_threshold=/:0");
        sleep(disk_check_wait);
        mxs.wait_for_monitor(1);

        // server2 was in maintenance before the switchover, so it was ignored. This means that it is
        // still replicating from server1. server1 was redirected to the new master. Although server1
        // is low on disk space, it is not set to maintenance since it is a relay.
        mxs.check_print_servers_status({slave | mxt::ServerInfo::RELAY, maint, master, slave});

        // Check that writes are working.
        auto maxconn = mxs.open_rwsplit_connection2();
        maxconn->cmd_f(insert_query, insert_val);

        mxs.wait_for_monitor();
        mxs.get_servers().print();

        log.log_msg("Changing disk space threshold for the monitor, should prevent low disk switchovers.");
        test.maxctrl("alter monitor MySQL-Monitor disk_space_threshold=/:99");
        sleep(disk_check_wait);
        mxs.wait_for_monitor(1);
    }

    // Use the reset-replication command to fix the situation.
    log.log_msg("Running reset-replication to fix the situation.");
    test.maxctrl("call command mariadbmon reset-replication MySQL-Monitor server1");
    sleep(disk_check_wait);
    mxs.wait_for_monitor(1);
    // Check that no auto switchover has happened.
    mxs.check_print_servers_status({master, maint, slave, slave});

    const char drop_query[] = "DROP TABLE test.t1;";
    auto maxconn = mxs.open_rwsplit_connection2();
    maxconn->cmd(drop_query);

    if (disks_plugin_loaded)
    {
        repl.ping_or_open_admin_connections();
        // Disable the disks-plugin on all servers.
        for (int i = 0; i < repl.N; i++)
        {
            auto conn = repl.backend(i)->admin_connection();
            conn->cmd("UNINSTALL SONAME 'disks';");
            conn->cmd_f(strict_mode, 0);
        }
    }
}
}

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    return TestConnections().run_test(argc, argv, test_main);
}
