/**
 * MXS-1743: Maxscale unable to enforce round-robin between read service for Slave
 *
 * https://jira.mariadb.org/browse/MXS-1743
 */
#include "testconnections.h"
#include <vector>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto do_test = [&]() {
            test.set_timeout(20);
            test.maxscales->connect();
            test.try_query(test.maxscales->conn_master[0], "SELECT 1");
            test.maxscales->disconnect();
            test.stop_timeout();
        };

    test.tprintf("Testing with both master and slave up");
    do_test();

    test.tprintf("Testing with only the master");
    test.repl->block_node(0);
    test.maxscales->wait_for_monitor();
    do_test();
    test.repl->unblock_node(0);
    test.maxscales->wait_for_monitor();

    test.tprintf("Testing with only the slave");
    test.repl->block_node(1);
    test.maxscales->wait_for_monitor();
    do_test();
    test.repl->unblock_node(1);
    test.maxscales->wait_for_monitor();

    test.tprintf("Checking that both the master and slave are used");
    std::vector<Connection> connections;

    test.tprintf("Opening new connections to verify readconnroute works");

    for (int i = 0; i < 20; i++)
    {
        test.set_timeout(20);
        connections.push_back(test.maxscales->readconn_master());
        Connection& c = connections.back();
        test.expect(c.connect(), "Connect should work: %s", c.error());
        test.expect(c.query("SELECT 1"), "Query should work: %s", c.error());
        test.stop_timeout();
    }

    auto s1 = test.maxscales->ssh_output("maxctrl --tsv list servers|grep server1|cut -f 4").second;
    auto s2 = test.maxscales->ssh_output("maxctrl --tsv list servers|grep server2|cut -f 4").second;

    test.expect(s1 == s2,
                "Master and slave shoud have the same amount of connections: %s != %s",
                s1.c_str(), s2.c_str());

    return test.global_result;
}
