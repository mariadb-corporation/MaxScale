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

#include "fail_switch_rejoin_common.cpp"
#include <iostream>

using std::string;
using std::cout;
using std::endl;

int main(int argc, char** argv)
{
    interactive = strcmp(argv[argc - 1], "interactive") == 0;
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);
    int ec;
    MYSQL* maxconn = test.maxscales->open_rwsplit_connection(0);
    // Set up test table
    basic_test(test);
    // Delete binlogs to sync gtid:s
    delete_slave_binlogs(test);
    char result_tmp[bufsize];
    // Advance gtid:s a bit to so gtid variables are updated.
    generate_traffic_and_check(test, maxconn, 10);
    mysql_close(maxconn);
    test.tprintf(LINE);
    print_gtids(test);
    get_input();

    cout << "Stopping master and waiting for failover. Check that another server is promoted." << endl;
    const int old_master_id = get_master_server_id(test);   // Read master id now before shutdown.
    const int master_index = test.repl->master;
    test.repl->stop_node(master_index);

    // Wait until failover is performed
    test.maxscales->wait_for_monitor(2);
    get_output(test);

    int master_id = get_master_server_id(test);
    cout << "Master server id is " << master_id << endl;
    const bool failover_ok = (master_id > 0 && master_id != old_master_id);
    test.expect(failover_ok, "Master did not change or no master detected.");

    if (test.ok())
    {
        // Recreate maxscale session
        maxconn = test.maxscales->open_rwsplit_connection(0);
        cout << "Sending more inserts." << endl;
        generate_traffic_and_check(test, maxconn, 5);
        print_gtids(test);
        cout << "Bringing old master back online..." << endl;
        test.repl->start_node(master_index, (char*) "");
        test.maxscales->wait_for_monitor();
        test.repl->connect();
        get_output(test);
        test.tprintf("and manually rejoining it to cluster.");
        const char REJOIN_CMD[] = "maxadmin call command mariadbmon rejoin MySQL-Monitor server1";
        test.maxscales->ssh_node_output(0, REJOIN_CMD, true, &ec);
        test.maxscales->wait_for_monitor();
        get_output(test);

        string gtid_old_master;
        if (find_field(test.repl->nodes[master_index], GTID_QUERY, GTID_FIELD, result_tmp) == 0)
        {
            gtid_old_master = result_tmp;
        }
        string gtid_final;
        if (find_field(maxconn, GTID_QUERY, GTID_FIELD, result_tmp) == 0)
        {
            gtid_final = result_tmp;
        }

        cout << LINE << "\n";
        print_gtids(test);
        cout << LINE << "\n";
        test.expect(gtid_final == gtid_old_master,
                    "Old master did not successfully rejoin the cluster (%s != %s).",
                    gtid_final.c_str(), gtid_old_master.c_str());
        // Switch master back to server1 so last check is faster
        int ec;
        test.maxscales->ssh_node_output(0, "maxadmin call command mysqlmon switchover "
                                        "MySQL-Monitor server1 server2", true, &ec);
        test.maxscales->wait_for_monitor();
        get_output(test);
        master_id = get_master_server_id(test);
        test.expect(master_id == old_master_id, "Switchover back to server1 failed.");

        // STOP and RESET SLAVE on a server, then remove binlogs. Check that a server with empty binlogs
        // can be rejoined.
        if (test.ok())
        {
            cout << "Removing slave connection and deleting binlogs on server3 to get empty gtid.\n";
            int slave_to_reset = 2;
            test.repl->connect();
            auto conn = test.repl->nodes[slave_to_reset];
            string sstatus_query = "SHOW ALL SLAVES STATUS;";
            test.try_query(conn,
                           "STOP SLAVE; RESET SLAVE ALL; RESET MASTER; SET GLOBAL gtid_slave_pos='';");
            test.maxscales->wait_for_monitor();
            get_output(test);
            auto row = get_row(conn, sstatus_query.c_str());
            test.expect(row.empty(), "server3 is still replicating.");
            row = get_row(conn, "SELECT @@gtid_current_pos;");
            test.expect(row.empty() || row[0].empty(),
                        "server3 gtid is not empty as it should (%s).", row[0].c_str());
            cout << "Rejoining server3.\n";
            test.maxscales->ssh_node_output(0, "maxadmin call command mysqlmon rejoin "
                                            "MySQL-Monitor server3", true, &ec);
            test.maxscales->wait_for_monitor();
            get_output(test);
            char result[100];
            if (find_field(conn, sstatus_query.c_str(), "Master_Host", result) == 0)
            {
		test.expect(strcmp(result, test.repl->IP[0]) == 0,
                            "server3 did not rejoin the cluster (%s != %s).", result, test.repl->IP[0]);
            }
            else
            {
                test.expect(false, "Could not query slave status.");
            }
            if (test.ok())
            {
                cout << "server3 joined succesfully, test complete.\n";
            }
        }
    }
    else
    {
        test.repl->start_node(master_index, (char*) "");
        test.maxscales->wait_for_monitor();
    }

    return test.global_result;
}
