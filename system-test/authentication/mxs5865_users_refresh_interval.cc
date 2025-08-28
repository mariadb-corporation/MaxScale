#include <maxtest/testconnections.hh>
#include <fstream>

int count_lines(TestConnections& test)
{
    test.maxscale->copy_from_node("/var/log/maxscale/maxscale.log", "./maxscale.log");
    std::ifstream infile("./maxscale.log");
    int found = 0;

    for (std::string line; std::getline(infile, line);)
    {
        found += line.find("user@host entries") != std::string::npos ? 1 : 0;
    }

    remove("./maxscale.log");
    return found;
}

void test_main(TestConnections& test)
{
    test.log_printf("Wait for a bit for things to stabilize");
    std::this_thread::sleep_for(5s);

    int before = count_lines(test);
    test.log_printf("Before: %d", before);

    test.check_maxctrl("alter maxscale users_refresh_interval=1s");
    test.log_printf("Altered to maxscale users_refresh_interval=1s");
    std::this_thread::sleep_for(10s);

    int after = count_lines(test);
    test.log_printf("After: %d", after);

    test.expect(after - before > 5, "Expected more than 5 updates of users, found only %d", after - before);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
