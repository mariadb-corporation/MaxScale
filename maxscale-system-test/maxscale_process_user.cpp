/**
 * Check if Maxscale priocess is running as 'maxscale'
 */

#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.set_timeout(50);
    auto res = test.maxscales->ssh_output("ps -U maxscale -C maxscale -o user --no-headers").second;
    res = res.substr(0, strlen("maxscale"));
    test.expect(res == "maxscale", "MaxScale running as '%s' instead of 'maxscale'", res.c_str());

    return test.global_result;
}
