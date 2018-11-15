/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "testconnections.h"
#include "fail_switch_rejoin_common.cpp"
#include <iostream>
#include <string>

using std::string;
using std::cout;

int main(int argc, char** argv)
{
    // Only in very recent server versions have the disks-plugin
    TestConnections::require_repl_version("10.3.6");
    Mariadb_nodes::require_gtid(true);
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    test.repl->connect();
    delete_slave_binlogs(test);

    // Enable the disks-plugin on all servers. Has to be done before MaxScale is on to prevent disk space
    // monitoring from disabling itself.
    bool disks_plugin_loaded = false;
    const char strict_mode[] = "SET GLOBAL gtid_strict_mode=%i;";
    test.repl->connect();
    for (int i = 0; i < test.repl->N; i++)
    {
        MYSQL* conn = test.repl->nodes[i];
        test.try_query(conn, "INSTALL SONAME 'disks';");
        test.try_query(conn, strict_mode, 1);
    }

    if (test.ok())
    {
        cout << "Disks-plugin installed and gtid_strict_mode enabled on all servers. "
                "Starting MaxScale.\n";
        test.start_maxscale();
        disks_plugin_loaded = true;
    }
    else
    {
        cout << "Test preparations failed.\n";
    }

    auto set_to_string = [](const StringSet& str_set) -> string {
            string rval;
            for (const string& elem : str_set)
            {
                rval += elem + ",";
            }
            return rval;
    };

    auto expect_server_status = [&test, &set_to_string](const string& server_name, const string& status) {
            auto status_set = test.maxscales->get_server_status(server_name.c_str());
            string status_str = set_to_string(status_set);
            bool found = (status_set.count(status) == 1);
            test.expect(found, "%s was not %s as was expected. Status: %s.",
                        server_name.c_str(), status.c_str(), status_str.c_str());
        };

    string server_names[] = {"server1", "server2", "server3", "server4"};
    string master = "Master";
    string slave = "Slave";
    string maint = "Maintenance";
    const char insert_query[] = "INSERT INTO test.t1 VALUES (%i);";
    int insert_val = 1;

    if (test.ok())
    {
        // Set up test table to ensure queries are going through.
        test.tprintf("Creating table and inserting data.");
        auto maxconn = test.maxscales->open_rwsplit_connection(0);
        test.try_query(maxconn, "CREATE OR REPLACE TABLE test.t1(c1 INT)");
        test.try_query(maxconn, insert_query, insert_val++);
        mysql_close(maxconn);

        get_output(test);
        print_gtids(test);

        expect_server_status(server_names[0], master);
        expect_server_status(server_names[1], maint); // Always out of disk space
        expect_server_status(server_names[2], slave);
        expect_server_status(server_names[3], slave);
    }

    if (test.ok())
    {
        // If ok so far, change the disk space threshold to something really small to force a switchover.
        cout << "Changing disk space threshold for the monitor, should cause a switchover.\n";
        test.maxscales->execute_maxadmin_command(0, "alter monitor MySQL-Monitor disk_space_threshold=/:1");
        sleep(2); // The disk space is checked depending on wall clock time.
        test.maxscales->wait_for_monitor(2);

        // server2 was in maintenance before the switchover, so it was ignored. This means that it is
        // still replicating from server1. server1 was redirected to the new master. Although server1
        // is low on disk space, it is not set to maintenance since it is a relay.
        expect_server_status(server_names[0], slave);
        expect_server_status(server_names[1], maint);
        expect_server_status(server_names[2], master);
        expect_server_status(server_names[3], slave);

        // Check that writes are working.
        auto maxconn = test.maxscales->open_rwsplit_connection(0);
        test.try_query(maxconn, insert_query, insert_val);
        mysql_close(maxconn);

        get_output(test);
        print_gtids(test);

        cout << "Changing disk space threshold for the monitor, should prevent low disk switchovers.\n";
        test.maxscales->execute_maxadmin_command(0, "alter monitor MySQL-Monitor "
                                                 "disk_space_threshold=/:100");
        sleep(2); // To update disk space status
        test.maxscales->wait_for_monitor(1);
        get_output(test);
    }

    // Use the reset-replication command to fix the situation.
    cout << "Running reset-replication to fix the situation.\n";
    test.maxscales->execute_maxadmin_command(0, "call command mariadbmon reset-replication "
                                                "MySQL-Monitor server1");
    sleep(2);
    test.maxscales->wait_for_monitor(2);
    get_output(test);
    // Check that no auto switchover has happened.
    expect_server_status(server_names[0], master);
    expect_server_status(server_names[1], maint);
    expect_server_status(server_names[2], slave);
    expect_server_status(server_names[3], slave);

    const char drop_query[] = "DROP TABLE test.t1;";
    auto maxconn = test.maxscales->open_rwsplit_connection(0);
    test.try_query(maxconn, drop_query);
    mysql_close(maxconn);

    if (disks_plugin_loaded)
    {
        // Enable the disks-plugin on all servers.
        for (int i = 0; i < test.repl->N; i++)
        {
            MYSQL* conn = test.repl->nodes[i];
            test.try_query(conn, "UNINSTALL SONAME 'disks';");
            test.try_query(conn, strict_mode, 0);
        }
    }

    test.repl->disconnect();
    return test.global_result;
}
