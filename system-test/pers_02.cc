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

/**
 * @file pers_02.cpp - Persistent connection test
 *
 * - Set max_connections to 20
 * - Open 75 connections to all Maxscale services
 * - Close connections
 * - Restart replication (stop all nodes and start them again, execute CHANGE MASTER TO again)
 * - Set max_connections to 2000
 * - Open 70 connections to all Maxscale services
 * - Close connections
 * - Check there is not crash during restart
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.repl->execute_query_all_nodes("set global max_connections = 20;");
    test.create_connections(75, true, true, true, false);

    test.repl->stop_nodes();
    test.repl->start_nodes();
    test.repl->disconnect();
    test.repl->sync_slaves();

    // Increase connection limits and wait a few seconds for the server to catch up
    test.repl->execute_query_all_nodes("set global max_connections = 2000;");
    test.maxscale->wait_for_monitor();

    test.add_result(test.create_connections(70, true, true, true, false),
                    "Connections creation error \n");

    test.check_maxscale_alive();
    return test.global_result;
}
