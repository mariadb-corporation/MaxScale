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
 * MXS-3342: Crash with proxy_protocol and persistent connections
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    std::string ip = test.maxscale->ip();
    test.repl->execute_query_all_nodes(("SET GLOBAL proxy_protocol_networks='" + ip + "'").c_str());
    test.maxscale->wait_for_monitor();

    Connection node = test.repl->get_connection(0);
    test.expect(node.connect(), "Connection should work: %s", node.error());
    test.expect(node.query("CREATE USER bob IDENTIFIED BY 'bob'"), "Query should work: %s", node.error());
    test.expect(node.query("GRANT ALL ON *.* TO bob"), "Query should work: %s", node.error());

    std::vector<Connection> connections;

    for (int i = 0; i < 100 && test.ok(); i++)
    {
        connections.emplace_back(test.maxscale->rwsplit());
        Connection& c = connections.back();
        c.set_credentials("bob", "bob");
        test.expect(c.connect(), "Readwritesplit connection should work: %s", c.error());
    }

    // Wait for some time to make sure the connection is fully established in
    // order for it to end up in the pool.
    sleep(5);
    connections.clear();
    sleep(5);

    auto res = test.repl->ssh_output("mariadb -u bob -pbob -h " + ip + " -P 4006 -e \"SELECT 1\"");
    test.expect(res.rc == 0, "Query from another IP should work: %d, %s", res.rc, res.output.c_str());

    test.repl->execute_query_all_nodes("SET GLOBAL proxy_protocol_networks=''");
    node.query("DROP USER bob");

    return test.global_result;
}
