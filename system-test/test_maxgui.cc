#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto res = test.maxscale->ssh_output("test -d /usr/share/maxscale/gui");
    test.expect(res.rc == 0, "GUI files not found at: /usr/share/maxscale/gui/");

    res = test.maxscale->ssh_output("curl -s -f localhost:8989/index.html");

    test.expect(res.rc == 0, "Root resource should serve the GUI main page.");
    test.expect(res.output.find("/js/") != std::string::npos,
                "GUI main page should load javascript: %s", res.output.c_str());

    return test.global_result;
}
