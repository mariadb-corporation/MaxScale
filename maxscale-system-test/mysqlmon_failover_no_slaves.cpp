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

#include "testconnections.h"
#include "fail_switch_rejoin_common.cpp"

int main(int argc, char** argv)
{
    interactive = strcmp(argv[argc - 1], "interactive") == 0;
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);

    delete_slave_binlogs(test);
    basic_test(test);
    MYSQL* conn = test.maxscales->open_rwsplit_connection(0);
    if (!generate_traffic_and_check(test, conn, 5))
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
    test.repl->start_node(2, (char *) "");
    // Slave 3. Create a second slave connection to a non-existing server.
    const char CHANGE_CMD[] = "CHANGE MASTER 'dummy' TO MASTER_HOST = 'imagination_host.img', "
    "MASTER_PORT = 1234, MASTER_USE_GTID = current_pos, MASTER_USER='repl', MASTER_PASSWORD='repl';";
    test.try_query(nodes[3], CHANGE_CMD);
    test.try_query(nodes[3], "START SLAVE;");

    sleep(4);
    get_output(test);

    test.tprintf(LINE);
    test.tprintf("Stopping master. Failover should not happen.");
    test.repl->block_node(0);
    sleep(10);
    get_output(test);
    int master_id = get_master_server_id(test);
    test.assert(master_id == -1, "Master was promoted even when no slave was eligible.");

    test.repl->unblock_node(0);
    sleep(1);

    // Restore normal settings.
    test.try_query(nodes[1], "START SLAVE;");
    test.repl->stop_node(2);
    test.repl->restore_server_settings(2);
    test.repl->start_node(2, (char *) "");
    test.try_query(nodes[3], "STOP SLAVE 'dummy';");
    test.try_query(nodes[3], "RESET SLAVE 'dummy' ALL;");
    test.repl->fix_replication();
    return test.global_result;
}
