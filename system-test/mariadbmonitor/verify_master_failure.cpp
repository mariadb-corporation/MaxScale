/**
 * MXS-1493: https://jira.mariadb.org/browse/MXS-1493
 *
 * Testing of master failure verification
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("Blocking master and checking that master failure is delayed at least once.");
    test.repl->block_node(0);
    test.maxscales->wait_for_monitor();
    test.log_includes(0, "If master does not return in .* monitor tick(s), failover begins.");

    test.tprintf("Waiting to see if failover is performed.");
    test.maxscales->wait_for_monitor(5);

    test.log_includes(0, "Performing.*failover");

    // TODO: Extend the test

    return test.global_result;
}
