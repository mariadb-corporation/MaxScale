/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

void test_main(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    c.connect();
    auto old_max_connections = "SET GLOBAL max_connections=" + c.field("SELECT @@global.max_connections");
    c.disconnect();

    test.repl->execute_query_all_nodes("SET GLOBAL max_connections=10");

    std::vector<Connection> conns;

    for (int i = 0; i < 20; i++)
    {
        conns.emplace_back(test.maxscale->rwsplit());

        if (!conns.back().connect() || !conns.back().query("SELECT 1"))
        {
            break;
        }
    }

    test.log_printf("Opening connection");
    test.expect(c.connect(), "Connection to MaxScale should work: %s", c.error());
    test.expect(!c.query("SELECT 1"), "Query should fail due to connection limit");

    conns.clear();
    test.expect(c.connect(), "Failed to connect after closing all connections: %s", c.error());
    c.query("DROP USER foo");

    test.repl->execute_query_all_nodes(old_max_connections.c_str());
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
