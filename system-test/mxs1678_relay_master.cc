/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-1678: Stopping IO thread on relay master causes it to be promoted as master
 *
 * https://jira.mariadb.org/browse/MXS-1678
 */
#include <maxtest/testconnections.hh>
#include <set>
#include <string>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.repl->connect();
    execute_query(test.repl->nodes[3], "STOP SLAVE");
    execute_query(test.repl->nodes[3],
                  "CHANGE MASTER TO MASTER_HOST='%s', MASTER_PORT=%d",
                  test.repl->ip_private(2),
                  test.repl->port(2));
    execute_query(test.repl->nodes[3], "START SLAVE");
    test.maxscale->wait_for_monitor();

    auto& mxs = *test.maxscale;
    auto master_st = mxt::ServerInfo::master_st;
    auto slave_st = mxt::ServerInfo::slave_st;
    auto relay = mxt::ServerInfo::RELAY | slave_st;
    auto running_st = mxt::ServerInfo::RUNNING;

    test.tprintf("Checking before stopping IO thread");
    mxs.check_print_servers_status({master_st, slave_st, relay, slave_st});

    execute_query(test.repl->nodes[2], "STOP SLAVE IO_THREAD");
    mxs.wait_for_monitor();

    test.tprintf("Checking after stopping IO thread");
    mxs.check_print_servers_status({master_st, slave_st, running_st, running_st});

    return test.global_result;
}
