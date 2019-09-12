/**
 * Test that service-to-service routing can be done at runtime and that the persisted configuration is valid
 */

#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto do_check = [&]() {
            test.maxscales->wait_for_monitor();
            auto conn = test.maxscales->rwsplit();
            test.expect(conn.connect(), "Connection should work: %s", conn.error());
            test.expect(conn.query("SELECT 1"), "SELECT should work: %s", conn.error());
            test.expect(conn.query("SET @a = 1"), "SET should work: %s", conn.error());
            test.expect(conn.query("CREATE TEMPORARY TABLE test.t1(id int)"),
                        "CREATE should work: %s", conn.error());
        };

    test.check_maxctrl("create service combined-service readconnroute user=maxskysql password=skysql");
    test.check_maxctrl("create listener combined-service listener1 4006");
    test.check_maxctrl("link service combined-service service1 service2");

    do_check();
    test.maxscales->restart();
    do_check();

    return test.global_result;
}
