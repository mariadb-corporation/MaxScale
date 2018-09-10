/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "fail_switch_rejoin_common.cpp"

using std::string;

int main(int argc, char** argv)
{
    interactive = strcmp(argv[argc - 1], "interactive") == 0;
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);
    MYSQL* maxconn = test.maxscales->open_rwsplit_connection(0);
    // Set up test table
    basic_test(test);
    // Delete binlogs to sync gtid:s
    delete_slave_binlogs(test);
    char result_tmp[bufsize];
    // Advance gtid:s a bit to so gtid variables are updated.
    generate_traffic_and_check(test, maxconn, 10);
    test.maxscales->wait_for_monitor();
    test.tprintf(LINE);
    print_gtids(test);
    get_input();

    test.tprintf("Stopping master and waiting for failover. Check that another server is promoted.");
    test.tprintf(LINE);
    const int old_master_id = get_master_server_id(test); // Read master id now before shutdown.
    const int master_index = test.repl->master;
    test.repl->stop_node(master_index);
    test.maxscales->wait_for_monitor();
    // Recreate maxscale session
    mysql_close(maxconn);
    maxconn = test.maxscales->open_rwsplit_connection(0);
    get_output(test);
    int master_id = get_master_server_id(test);
    test.tprintf(LINE);
    test.tprintf(PRINT_ID, master_id);
    const bool failover_ok = (master_id > 0 && master_id != old_master_id);
    test.expect(failover_ok, "Master did not change or no master detected.");
    string gtid_final;
    if (failover_ok)
    {
        test.tprintf("Sending more inserts.");
        generate_traffic_and_check(test, maxconn, 5);
        test.maxscales->wait_for_monitor();
        if (find_field(maxconn, GTID_QUERY, GTID_FIELD, result_tmp) == 0)
        {
            gtid_final = result_tmp;
        }
        print_gtids(test);
        test.tprintf("Bringing old master back online. It should rejoin the cluster and catch up in events.");
        test.tprintf(LINE);

        test.repl->start_node(master_index, (char*) "");
        test.maxscales->wait_for_monitor();
        get_output(test);

        test.repl->connect();
        test.maxscales->wait_for_monitor();
        string gtid_old_master;
        if (find_field(test.repl->nodes[master_index], GTID_QUERY, GTID_FIELD, result_tmp) == 0)
        {
            gtid_old_master = result_tmp;
        }
        test.tprintf(LINE);
        print_gtids(test);
        test.tprintf(LINE);
        test.expect(gtid_final == gtid_old_master, "Old master did not successfully rejoin the cluster.");
        // Switch master back to server1 so last check is faster
        int ec;
        test.maxscales->ssh_node_output(0, "maxadmin call command mysqlmon switchover "
                                        "MySQL-Monitor server1 server2" , true, &ec);
        test.maxscales->wait_for_monitor(); // Wait for monitor to update status
        get_output(test);
        master_id = get_master_server_id(test);
        test.expect(master_id == old_master_id, "Switchover back to server1 failed.");
    }
    else
    {
        test.repl->start_node(master_index, (char*) "");
        test.maxscales->wait_for_monitor();
    }

    test.repl->fix_replication();
    return test.global_result;
}
