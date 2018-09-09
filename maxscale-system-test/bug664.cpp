/**
 * @file bug664.cpp Tee filter branch session failure test
 *
 * - Configure MaxScale so that the branched session will always fail
 * - Execute query on the main service and check that MaxScale is alive
 * - An error should be logged about the failed branch session
 */

#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.check_maxscale_alive(0);
    test.check_log_err(0, "Failed to create new router session for service 'RW_Split'", true);
    return test.global_result;
}
