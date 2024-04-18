#include <maxtest/testconnections.hh>

void test_main(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    c.connect();

    for (int i = 0; i < 100 && test.ok(); i++)
    {
        std::string user = "test" + std::to_string(i);
        c.query("CREATE USER '" + user + "'@'%' IDENTIFIED BY 'pw'");

        auto u = test.maxscale->rwsplit("");
        u.set_credentials(user, "pw");
        test.expect(u.connect(), "Failed to connect: %s", u.error());
        test.expect(u.query("SELECT 1"), "Failed to query: %s", u.error()),

        c.query("DROP USER '" + user + "'@'%'");
    }
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
