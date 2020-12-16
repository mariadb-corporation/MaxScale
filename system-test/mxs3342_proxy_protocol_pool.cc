/**
 * MXS-3342: Crash with proxy_protocol and persistent connections
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    std::string ip = test.maxscales->ip();
    test.repl->execute_query_all_nodes(("SET GLOBAL proxy_protocol_networks='" + ip + "'").c_str());

    Connection node = test.repl->get_connection(0);
    test.expect(node.connect(), "Connection should work: %s", node.error());
    test.expect(node.query("CREATE USER bob IDENTIFIED BY 'bob'"), "Query should work: %s", node.error());
    test.expect(node.query("GRANT ALL ON *.* TO bob"), "Query should work: %s", node.error());

    std::vector<Connection> connections;

    for (int i = 0; i < 100 && test.ok(); i++)
    {
        connections.emplace_back(test.maxscales->rwsplit());
        Connection& c = connections.back();
        c.set_credentials("bob", "bob");
        test.expect(c.connect(), "Connection should work: %s", c.error());
    }

    // Wait for some time to make sure the connection is fully established in
    // order for it to end up in the pool.
    sleep(5);
    connections.clear();
    sleep(5);

    auto res = test.repl->ssh_output("mysql -u bob -pbob -h " + ip + " -P 4006 -e \"SELECT 1\"");
    test.expect(res.rc == 0, "Query from another IP should work: %d, %s", res.rc, res.output.c_str());

    test.repl->execute_query_all_nodes("SET GLOBAL proxy_protocol_networks=''");
    node.query("DROP USER bob");

    return test.global_result;
}
