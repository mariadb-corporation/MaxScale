/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file slave_failover.cpp  Check how Maxscale works in case of one slave failure, only one slave is
 * configured
 *
 * - Connect to RWSplit
 * - find which backend slave is used for connection
 * - blocm mariadb on the slave with firewall
 * - wait 60 seconds
 * - check which slave is used for connection now, expecting any other slave
 * - check warning in the error log about broken slave
 * - unblock mariadb backend (restore slave firewall settings)
 * - check if Maxscale still alive
 */


#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.reset_timeout();
    test.repl->connect();
    auto ids = test.repl->get_all_server_ids();
    test.repl->disconnect();

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection to rwsplit should work: %s", conn.error());

    auto first_slave = conn.field("SELECT @@server_id");
    conn.disconnect();

    test.expect(!first_slave.empty(), "Result should not be empty");
    int slave = std::stoi(first_slave);
    test.expect(slave != ids[0], "The result should not be from the master");

    test.reset_timeout();
    for (int i = 1; i < test.repl->N; i++)
    {
        if (ids[i] == slave)
        {
            test.repl->block_node(i);
            test.maxscale->wait_for_monitor();
            break;
        }
    }

    test.reset_timeout();
    test.expect(conn.connect(), "Connection to rwsplit should work: %s", conn.error());
    auto second_slave = conn.field("SELECT @@server_id");

    test.expect(!second_slave.empty(), "Second result should not be empty");
    test.expect(first_slave != second_slave, "The slave should change");
    slave = std::stoi(second_slave);
    test.expect(slave != ids[0], "The result should not be from the master");

    for (int i = 1; i < test.repl->N; i++)
    {
        if (ids[i] == slave)
        {
            test.repl->unblock_node(i);
            break;
        }
    }

    return test.global_result;
}
