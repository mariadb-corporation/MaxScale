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

/**
 * Connect to readconnroute in master mode and check that it always connects to the master
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.reset_timeout();

    test.repl->connect();
    test.tprintf("Connecting to ReadConnnRouter in 'master' mode");
    test.maxscale->connect_readconn_master();
    auto master = get_row(test.repl->nodes[0], "SELECT @@server_id");
    auto maxscale = get_row(test.maxscale->conn_master, "SELECT @@server_id");
    test.expect(master == maxscale, "Connection did not go to the master: %s", maxscale[0].c_str());
    test.maxscale->close_readconn_master();

    test.tprintf("Changing master to node 1");
    test.reset_timeout();
    test.repl->change_master(1, 0);
    test.maxscale->wait_for_monitor();

    test.tprintf("Connecting to ReadConnnRouter in 'master' mode");
    test.reset_timeout();
    test.maxscale->connect_readconn_master();
    master = get_row(test.repl->nodes[1], "SELECT @@server_id");
    maxscale = get_row(test.maxscale->conn_master, "SELECT @@server_id");
    test.expect(master == maxscale, "Connection did not go to the master: %s", maxscale[0].c_str());
    test.maxscale->close_readconn_master();

    test.repl->change_master(0, 1);
    test.log_excludes("The service 'CLI' is missing a definition of the servers");

    return test.global_result;
}
