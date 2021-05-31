/**
 * MXS-2456: Cap transaction replay attempts
 * https://jira.mariadb.org/browse/MXS-2456
 */

#include <maxtest/testconnections.hh>

#define EXPECT(a) test.expect(a, "%s", "Assertion failed: " #a)

void test_replay_ok(TestConnections& test)
{
    Connection c = test.maxscale->rwsplit();
    EXPECT(c.connect());
    EXPECT(c.query("BEGIN"));
    EXPECT(c.query("SELECT 1"));
    EXPECT(c.query("SELECT SLEEP(15)"));

    // Block the node where the transaction was started
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor();
    sleep(5);

    // Then block the node where the transaction replay is attempted before the last statement finishes
    test.repl->block_node(1);
    test.maxscale->wait_for_monitor();
    sleep(5);

    // The next query should succeed as we do two replay attempts
    test.expect(c.query("SELECT 2"), "Two transaction replays should work");

    // Reset the replication
    test.repl->unblock_node(1);
    test.repl->unblock_node(0);
    test.maxscale->wait_for_monitor();
    test.check_maxctrl("call command mariadbmon reset-replication MariaDB-Monitor server1");
    test.maxscale->wait_for_monitor();
}

void test_replay_failure(TestConnections& test)
{
    Connection c = test.maxscale->rwsplit();
    c.connect();
    c.query("BEGIN");
    c.query("SELECT 1");
    c.query("SELECT SLEEP(15)");

    // Block the node where the transaction was started
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor();
    sleep(5);

    // Then block the node where the first transaction replay is attempted
    test.repl->block_node(1);
    test.maxscale->wait_for_monitor();
    sleep(5);

    // Block the final node before the replay completes
    test.repl->block_node(2);
    test.maxscale->wait_for_monitor();
    sleep(5);

    // The next query should fail as we exceeded the cap of two replays
    test.expect(!c.query("SELECT 2"), "Three transaction replays should NOT work");

    // Reset the replication
    test.repl->unblock_node(2);
    test.repl->unblock_node(1);
    test.repl->unblock_node(0);
    test.maxscale->wait_for_monitor();
    test.check_maxctrl("call command mariadbmon reset-replication MariaDB-Monitor server1");
    test.maxscale->wait_for_monitor();
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test_replay_ok(test);
    test_replay_failure(test);

    return test.global_result;
}
