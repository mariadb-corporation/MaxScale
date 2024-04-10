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

/**
 * MXS-2520: Allow master reconnection on reads
 * https://jira.mariadb.org/browse/MXS-2520
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.maxscale->connect();
    std::thread thr([&]() {
                        sleep(5);
                        test.tprintf("block node 0");
                        test.repl->block_node(0);
                        test.tprintf("wait for monitor");
                        test.maxscale->wait_for_monitor(2);
                        test.tprintf("unblock node 0");
                        test.repl->unblock_node(0);
                    });

    test.reset_timeout();
    test.tprintf("SELECT SLEEP(10)");
    test.try_query(test.maxscale->conn_rwsplit, "SELECT SLEEP(10)");

    test.tprintf("disconnect");
    test.maxscale->disconnect();
    test.tprintf("join");
    thr.join();

    return test.global_result;
}
