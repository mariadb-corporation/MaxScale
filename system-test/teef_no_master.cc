/**
 * @file bug664.cpp Tee filter branch session failure test
 *
 * - Configure MaxScale so that the branched session will always fail
 * - Execute query on the main service and check that MaxScale is alive
 * - An error should be logged about the failed branch session
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.check_maxscale_alive();
    test.log_includes("Failed to create new router session for service 'RW_Split'");
    return test.global_result;
}
