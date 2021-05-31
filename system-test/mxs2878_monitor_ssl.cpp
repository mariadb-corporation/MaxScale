/**
 * Covers the following bugs:
 * MXS-2878: Monitor connections do not insist on SSL being used
 * MXS-2896: Server wrongly in Running state after failure to connect
 */

#include <maxtest/testconnections.hh>
#include <sstream>

std::string join(StringSet st)
{
    std::ostringstream ss;

    for (const auto& a : st)
    {
        ss << a << " ";
    }

    return ss.str();
}

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);
    test.repl->disable_ssl();
    test.maxscale->restart();

    for (auto srv : {"server1", "server2", "server3", "server4"})
    {
        StringSet expected = {"Down"};
        auto status = test.maxscale->get_server_status(srv);
        test.expect(status == expected,
                    "Expected '%s' but got '%s'", join(expected).c_str(), join(status).c_str());
    }

    return test.global_result;
}
