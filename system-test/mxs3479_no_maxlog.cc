/**
 * MXS-3479: maxlog=false doesn't suppress all logging
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto result = test.maxscale->ssh_output("cat /var/log/maxscale/maxscale.log");
    test.expect(result.output.empty(), "Log file is not empty");

    return test.global_result;
}
