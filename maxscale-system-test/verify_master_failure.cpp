/**
 * MXS-1493: https://jira.mariadb.org/browse/MXS-1493
 *
 * Testing of master failure verification
 */

#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("Blocking master and checking that master failure is delayed at least once.");
    test.repl->block_node(0);
    sleep(5);
    test.check_log_err("delaying failover", true);

    test.tprintf("Waiting to see if failover is performed.");
    sleep(10);
    test.check_log_err("Performing failover", true);

    // TODO: Extend the test

    return test.global_result;
}
