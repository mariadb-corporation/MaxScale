#include <maxtest/testconnections.hh>

void check_connections(TestConnections& test, const std::vector<int>& expected)
{
    test.maxscale().get_servers().check_connections(expected);
}

void cycle_master(TestConnections& test)
{
    test.repl->block_node(0);
    test.maxscales->wait_for_monitor(2);
    test.repl->unblock_node(0);
    test.maxscales->wait_for_monitor(2);
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    std::vector<Connection> connections;

    test.tprintf("Create 10 connections");

    for (int i = 0; i < 10; i++)
    {
        Connection c = test.maxscales->rwsplit();
        test.expect(c.connect(), "Failed to connect: %s", c.error());
        connections.push_back(std::move(c));
    }

    test.tprintf("Expecting 10 connections on server1");
    check_connections(test, {10, 0, 0, 0});

    test.tprintf("Restart master, expect no connections");
    cycle_master(test);

    check_connections(test, {0, 0, 0, 0});

    test.tprintf("Enable transaction_replay and reconnect");
    test.check_maxctrl("alter service RW-Split-Router transaction_replay true");

    for (auto& c : connections)
    {
        test.expect(c.connect(), "Failed to connect: %s", c.error());
    }

    check_connections(test, {10, 0, 0, 0});

    test.tprintf("Restart master, expecting 10 connections on server1");
    cycle_master(test);

    for (auto& c : connections)
    {
        test.expect(c.query("SELECT 1"), "Read failed: %s", c.error());
        test.expect(c.query("SELECT @@last_insert_id"), "Write failed: %s", c.error());
    }

    check_connections(test, {10, 0, 0, 0});

    test.tprintf("Switch master to server2, expecting 10 connections on server2");
    test.check_maxctrl("call command mariadbmon switchover MariaDB-Monitor server2");

    for (auto& c : connections)
    {
        test.expect(c.query("SELECT 1"), "Read failed: %s", c.error());
        test.expect(c.query("SELECT @@last_insert_id"), "Write failed: %s", c.error());
    }

    check_connections(test, {0, 10, 0, 0});

    test.tprintf("Switch master to server1, expecting 10 connections on server1");
    test.check_maxctrl("call command mariadbmon switchover MariaDB-Monitor server1");

    for (auto& c : connections)
    {
        test.expect(c.query("SELECT 1"), "Read failed: %s", c.error());
        test.expect(c.query("SELECT @@last_insert_id"), "Write failed: %s", c.error());
    }

    check_connections(test, {10, 0, 0, 0});

    test.tprintf("Close all connections, expect no connections");
    connections.clear();
    check_connections(test, {0, 0, 0, 0});

    return test.global_result;
}
