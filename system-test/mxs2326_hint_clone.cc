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
 * MXS-2326: Routing hints aren't cloned in gwbuf_clone_shallow
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    Connection c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Connection should work: %s", c.error());

    std::string correct_id = c.field("SELECT @@server_id -- maxscale route to server server4");

    test.tprintf("Executing session command");
    test.expect(c.query("SET @a = 1"), "SET should work: %s", c.error());

    test.tprintf("Forcing a reconnection to occur on the next query by blocking the server");
    test.repl->block_node(3);
    test.maxscale->wait_for_monitor();
    test.repl->unblock_node(3);
    test.maxscale->wait_for_monitor();

    test.tprintf("Executing a query with a routing hint to a server that the session is not connected to");
    test.expect(c.check("SELECT @@server_id -- maxscale route to server server4",
                        correct_id), "Hint should be routed to the same server");

    return test.global_result;
}
