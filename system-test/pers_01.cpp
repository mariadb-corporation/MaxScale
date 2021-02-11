/**
 * @file pers_01.cpp - Persistent connection test
 * Open 70 connections to all Maxscale services
 * Close connections
 * Check that connection pool behaves as expected as time passes
 */


#include <maxtest/testconnections.hh>

using IntVector = std::vector<int>;

void check_conn_pool_size(TestConnections& test, const IntVector& expected)
{
    auto& mxs = test.maxscale();
    auto info = mxs.get_servers();
    info.check_pool_connections(expected);
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.add_result(test.create_connections(0, 70, true, true, true, false),
                    "Error creating connections");
    sleep(5);
    test.set_timeout(20);

    test.tprintf("Test 1:");
    IntVector expected = {1, 5, 10, 30};
    check_conn_pool_size(test, expected);

    test.stop_timeout();

    test.tprintf("Sleeping 10 seconds");
    sleep(10);

    test.set_timeout(20);
    test.tprintf("Test 2:");
    check_conn_pool_size(test, expected);

    test.tprintf("Sleeping 30 seconds");
    test.stop_timeout();
    sleep(30);

    test.set_timeout(20);
    test.tprintf("Test 3:");

    expected = {1, 5, 10, 0};
    check_conn_pool_size(test, expected);

    test.tprintf("Sleeping 30 seconds");
    test.stop_timeout();
    sleep(30);
    test.set_timeout(20);

    test.tprintf("Test 4:");

    expected = {1, 0, 0, 0};
    check_conn_pool_size(test, expected);

    return test.global_result;
}
