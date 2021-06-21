/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include "fail_switch_rejoin_common.cpp"
#include <iostream>
#include <string>

using std::string;
using std::cout;

int main(int argc, char** argv)
{
    // Only in very recent server versions have the disks-plugin
    TestConnections::require_repl_version("10.3.6");
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    test.repl->connect();
    const int N = 4;
    auto& mxs = *test.maxscale;
    auto& log = test.logger();

    // Enable the disks-plugin on all servers. Has to be done before MaxScale is on to prevent disk space
    // monitoring from disabling itself.
    bool disks_plugin_loaded = false;
    const char strict_mode[] = "SET GLOBAL gtid_strict_mode=%i;";
    test.repl->connect();
    for (int i = 0; i < N; i++)
    {
        MYSQL* conn = test.repl->nodes[i];
        test.try_query(conn, "INSTALL SONAME 'disks';");
        test.try_query(conn, strict_mode, 1);
    }

    if (test.ok())
    {
        cout << "Disks-plugin installed and gtid_strict_mode enabled on all servers. "
                "Starting MaxScale.\n";
        mxs.start_and_check_started();
        sleep(2);
        mxs.wait_for_monitor(2);
        disks_plugin_loaded = true;
    }
    else
    {
        cout << "Test preparations failed.\n";
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
        auto maxconn = test.maxscale->open_rwsplit_connection();
        test.try_query(maxconn, "CREATE OR REPLACE TABLE test.t1(c1 INT)");
        test.try_query(maxconn, insert_query, insert_val++);
        mysql_close(maxconn);

        auto status = mxs.get_servers();
        status.print();
        // server2 is always out of disk space.
        status.check_servers_status({master, maint, slave, slave});
    }

    if (test.ok())
    {
        // If ok so far, change the disk space threshold to something really small to force a switchover.
        log.log_msg("Changing disk space threshold for the monitor, should cause a switchover.");
        test.maxctrl("alter monitor MySQL-Monitor disk_space_threshold /:1");
        sleep(2);   // The disk space is checked depending on wall clock time.
        mxs.wait_for_monitor(2);

        // server2 was in maintenance before the switchover, so it was ignored. This means that it is
        // still replicating from server1. server1 was redirected to the new master. Although server1
        // is low on disk space, it is not set to maintenance since it is a relay.
        mxs.check_servers_status({slave | mxt::ServerInfo::RELAY, maint, master, slave});

        // Check that writes are working.
        auto maxconn = mxs.open_rwsplit_connection();
        test.try_query(maxconn, insert_query, insert_val);
        mysql_close(maxconn);

        mxs.wait_for_monitor();
        mxs.get_servers().print();

        log.log_msg("Changing disk space threshold for the monitor, should prevent low disk switchovers.");
        test.maxctrl("alter monitor MySQL-Monitor disk_space_threshold /:100");
        sleep(2);   // To update disk space status
        mxs.wait_for_monitor(1);
    }

    // Use the reset-replication command to fix the situation.
    log.log_msg("Running reset-replication to fix the situation.");
    test.maxctrl("call command mariadbmon reset-replication MySQL-Monitor server1");
    sleep(2);
    mxs.wait_for_monitor(2);
    auto status = mxs.get_servers();
    status.print();

    // Check that no auto switchover has happened.
    status.check_servers_status({master, maint, slave, slave});

    const char drop_query[] = "DROP TABLE test.t1;";
    auto maxconn = mxs.open_rwsplit_connection();
    test.try_query(maxconn, drop_query);
    mysql_close(maxconn);

    if (disks_plugin_loaded)
    {
        test.repl->connect();
        // Disable the disks-plugin on all servers.
        for (int i = 0; i < N; i++)
        {
            MYSQL* conn = test.repl->nodes[i];
            test.try_query(conn, "UNINSTALL SONAME 'disks';");
            test.try_query(conn, strict_mode, 0);
        }
    }

    test.repl->disconnect();
    return test.global_result;
}
