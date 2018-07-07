/**
 * Test runtime modification of router options
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.maxscales->wait_for_monitor();

    // Open a connection before and after setting master_failure_mode to fail_on_write
    Connection first = test.maxscales->rwsplit();
    Connection second = test.maxscales->rwsplit();
    Connection third = test.maxscales->rwsplit();
    test.maxscales->wait_for_monitor();

    first.connect();
    test.maxscales->ssh_node_f(0, true, "maxctrl alter service RW-Split-Router master_failure_mode fail_on_write");
    second.connect();

    // Check that writes work for both connections
    test.assert(first.query("SELECT @@last_insert_id"),
                "Write to first connection should work: %s", first.error());
    test.assert(second.query("SELECT @@last_insert_id"),
                "Write to second connection should work: %s", second.error());

    // Block the master
    test.repl->block_node(0);
    test.maxscales->wait_for_monitor();

    // Check that reads work for the newer connection and fail for the older one
    test.assert(!first.query("SELECT 1"),
                "Read to first connection should fail.");
    test.assert(second.query("SELECT 1"),
                "Read to second connection should work: %s", second.error());

    // Unblock the master, restart Maxscale and check that changes are persisted
    test.repl->unblock_node(0);
    test.maxscales->wait_for_monitor();
    test.maxscales->restart();

    third.connect();
    test.assert(third.query("SELECT @@last_insert_id"),
                "Write to third connection should work: %s", third.error());

    test.repl->block_node(0);
    test.maxscales->wait_for_monitor();

    test.assert(third.query("SELECT 1"),
                "Read to third connection should work: %s", third.error());

    test.repl->unblock_node(0);

    return test.global_result;
}
