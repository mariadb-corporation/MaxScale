/**
 * MXS-1493: https://jira.mariadb.org/browse/MXS-1493
 *
 * Testing of master failure verification
 */

#include "testconnections.h"

int main(int argc, char* argv[])
{
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);

    test.tprintf("Blocking master and checking that master failure is delayed at least once.");
    test.repl->block_node(0);
    sleep(5);
    test.log_includes(0, "Delaying failover");

    test.tprintf("Waiting to see if failover is performed.");
    sleep(10);

    test.log_includes(0, "Performing.*failover");

    // TODO: Extend the test

    return test.global_result;
}
