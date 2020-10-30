/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// MXS-2652: https://jira.mariadb.org/browse/MXS-2652

#include <maxtest/testconnections.hh>
#include "fail_switch_rejoin_common.cpp"
using std::string;

namespace
{
void expect_maintenance(TestConnections& test, std::string server_name, bool value);
void expect_running(TestConnections& test, std::string server_name, bool value);

const string running = "Running";
const string down = "Down";
const string maint = "Maintenance";
}

int main(int argc, char** argv)
{
    interactive = strcmp(argv[argc - 1], "interactive") == 0;
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);

    delete_slave_binlogs(test);
    basic_test(test);
    MYSQL* conn = test.maxscales->open_rwsplit_connection(0);
    bool success = generate_traffic_and_check(test, conn, 5);
    mysql_close(conn);
    if (!success)
    {
        return test.global_result;
    }

    // Make all three slaves ineligible for promotion in different ways.
    test.repl->connect();
    MYSQL** nodes = test.repl->nodes;
    // Slave 1. Just stop slave.
    test.try_query(nodes[1], "STOP SLAVE;");
    // Slave 2. Disable binlog.
    test.repl->stop_node(2);
    test.repl->stash_server_settings(2);
    test.repl->disable_server_setting(2, "log-bin");
    test.repl->start_node(2, (char*) "");
    test.maxscales->wait_for_monitor();

    // Slave 3. Set node to maintenance, then shut it down. Simultaneously check issue
    // MXS-2652: Maintenance flag should persist when server goes down & comes back up.
    int server_ind = 3;
    int server_num = server_ind + 1;
    string server_name = "server" + std::to_string(server_num);
    expect_maintenance(test, server_name, false);

    if (test.ok())
    {
        test.maxctrl("set server " + server_name + " maintenance");
        test.maxscales->wait_for_monitor();
        expect_running(test, server_name, true);
        expect_maintenance(test, server_name, true);

        test.repl->stop_node(server_ind);
        test.maxscales->wait_for_monitor();
        expect_running(test, server_name, false);
        expect_maintenance(test, server_name, true);

        test.repl->start_node(server_ind);
        test.maxscales->wait_for_monitor();
        expect_running(test, server_name, true);
        expect_maintenance(test, server_name, true);
    }

    get_output(test);

    test.tprintf(LINE);
    test.tprintf("Stopping master. Failover should not happen.");
    test.repl->block_node(0);
    test.maxscales->wait_for_monitor();
    get_output(test);
    int master_id = get_master_server_id(test);
    test.expect(master_id == -1, "Master was promoted even when no slave was eligible.");

    test.repl->unblock_node(0);

    // Restore normal settings.
    test.try_query(nodes[1], "START SLAVE;");
    test.repl->stop_node(2);
    test.repl->restore_server_settings(2);
    test.repl->start_node(2, (char*) "");
    test.maxctrl("clear server " + server_name + " maintenance");

    test.repl->fix_replication();
    return test.global_result;
}

namespace
{
void expect_running(TestConnections& test, std::string server_name, bool value)
{
    auto states = test.get_server_status(server_name.c_str());
    if (value)
    {
        test.expect(states.count(running) == 1, "'%s' is not running when it should be.",
                    server_name.c_str());
    }
    else
    {
        test.expect(states.count(down) == 1, "'%s' is not down when it should be.",
                    server_name.c_str());
    }
}

void expect_maintenance(TestConnections& test, std::string server_name, bool value)
{
    auto states = test.get_server_status(server_name.c_str());
    if (value)
    {
        test.expect(states.count(maint) == 1, "'%s' is not in maintenance when it should be.",
                    server_name.c_str());
    }
    else
    {
        test.expect(states.count(maint) == 0, "'%s' is in maintenance when it should not be.",
                    server_name.c_str());
    }
}
}
