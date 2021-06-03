/**
 * @file mxs922_double_listener.cpp MXS-922: Double creation of listeners
 *
 * Check that MaxScale doesn't crash when the same listeners are created twice.
 */

#include <maxtest/config_operations.hh>

int main(int argc, char* argv[])
{
    TestConnections* test = new TestConnections(argc, argv);
    Config config(test);

    config.create_all_listeners();
    config.create_all_listeners();
    test->maxscale->expect_running_status(true);

    config.create_monitor("mysql-monitor", "mysqlmon", 500);
    config.reset();

    sleep(1);

    test->check_maxscale_alive();
    int rval = test->global_result;
    delete test;
    return rval;
}
