/**
 * Test that service-to-service routing can be done at runtime and that the persisted configuration is valid
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto do_check = [&]() {
            test.maxscale->wait_for_monitor();
            auto conn = test.maxscale->rwsplit();
            test.expect(conn.connect(), "Connection should work: %s", conn.error());
            test.expect(conn.query("SELECT 1"), "SELECT should work: %s", conn.error());
            test.expect(conn.query("SET @a = 1"), "SET should work: %s", conn.error());
            test.expect(conn.query("CREATE TEMPORARY TABLE test.t1(id int)"),
                        "CREATE should work: %s", conn.error());
        };

    test.log_printf("Create service that uses other services");

    test.check_maxctrl("create service combined-service readconnroute user=maxskysql password=skysql");
    test.check_maxctrl("create listener combined-service listener1 4006");
    test.check_maxctrl("link service combined-service service1 service2");

    do_check();
    test.maxscale->restart();
    do_check();

    {
        test.log_printf("Open connection to combined-service and remove sub-service while the connection is still open");

        auto conn = test.maxscale->rwsplit();
        test.expect(conn.connect(), "Connection should work: %s", conn.error());

        test.check_maxctrl("unlink service combined-service service2");
        test.check_maxctrl("unlink service service2 server3 server4");
        test.check_maxctrl("destroy service service2");

        test.log_printf("Make sure the connection still works");

        test.expect(conn.query("SELECT 1"), "SELECT should work: %s", conn.error());
        test.expect(conn.query("SET @a = 1"), "SET should work: %s", conn.error());
        test.expect(conn.query("CREATE TEMPORARY TABLE test.t1(id int)"),
                    "CREATE should work: %s", conn.error());
    }

    // Now that the last active connection to service2 is closed, the service should've been destroyed

    test.log_printf("Make sure other connections work and don't use the removed service");

    auto other = test.maxscale->rwsplit();
    test.expect(other.connect(), "Other connection should work: %s", other.error());
    test.expect(other.query("SELECT 1"), "SELECT should work: %s", other.error());
    test.expect(other.query("SET @a = 1"), "SET should work: %s", other.error());

    return test.global_result;
}
