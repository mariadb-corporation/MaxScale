/**
 * Check if Maxscale priocess is running as 'maxscale'
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.reset_timeout();
    auto res = test.maxscale->ssh_output("ps -U maxscale -C maxscale -o user --no-headers").output;
    res = res.substr(0, strlen("maxscale"));
    test.expect(res == "maxscale", "MaxScale running as '%s' instead of 'maxscale'", res.c_str());

    return test.global_result;
}
