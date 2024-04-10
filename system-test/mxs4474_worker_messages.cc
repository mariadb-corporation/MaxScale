/*
 * Copyright (c) 2022 MariaDB Corporation Ab
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

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.repl->execute_query_all_nodes("SET GLOBAL max_connections = 10000");

    auto idle = test.maxscale->rwsplit();
    test.expect(idle.connect(), "Failed to create first connection: %s", idle.error());
    uint32_t id = idle.thread_id();

    std::vector<Connection> connections;

    // We'll probably run into some file descriptor limits before we create the connections. If we don't, try
    // to keep it at a reasonable level.
    for (int i = 0; i < 9000; i++)
    {
        auto c = test.maxscale->rwsplit();

        if (c.connect() && c.query("SELECT 1"))
        {
            connections.push_back(std::move(c));
        }
        else
        {
            break;
        }
    }

    test.tprintf("Managed to create %lu connections through MaxScale", connections.size());

    for (auto& c : connections)
    {
        test.expect(c.send_query("USE test"), "Sending USE should work: %s", c.error());
    }

    for (auto& c : connections)
    {
        test.expect(c.read_query_result(), "Reading USE result should work: %s", c.error());
    }

    for (auto& c : connections)
    {
        test.expect(c.send_query("KILL " + std::to_string(id)), "Sending KILL should work: %s", c.error());
    }

    for (auto& c : connections)
    {
        // The KILL might fail due to the MainWorker being overloaded but
        // the request itself should not time out.
        c.read_query_result();
    }

    return test.global_result;
}
