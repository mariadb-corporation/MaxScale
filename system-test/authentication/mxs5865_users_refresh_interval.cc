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

    test.log_printf("MXS-5983: Default users_refresh_interval causes repeated user account loading");
    test.maxscale->ssh_node_f(
        true, "find /var/lib/maxscale/maxscale.cnf.d -delete -mindepth 1");
    test.maxscale->ssh_node_f(
        true, "sed -i -e 's/users_refresh_interval=60s/users_refresh_interval=0s/' /etc/maxscale.cnf");

    test.maxscale->restart();
    test.log_printf("Wait for a bit for things to stabilize");
    std::this_thread::sleep_for(5s);

    auto c = test.maxscale->rwsplit();
    c.set_credentials("foo", "bar");
    int initial = count_lines(test);
    auto dur = 15s;

    for (auto start = std::chrono::steady_clock::now(); std::chrono::steady_clock::now() - start < dur;)
    {
        c.connect();
        std::this_thread::sleep_for(250ms);
    }

    int after_logins = count_lines(test);
    test.log_printf("Users were reloaded %d times during the last %ld seconds",
                    after_logins - initial, dur.count());
    test.expect(after_logins - initial < 7, "Users should be loaded less than 7 times");
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
