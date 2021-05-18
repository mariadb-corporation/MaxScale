/**
 * @file pers_01.cpp - Persistent connection test
 * Open 70 connections to all Maxscale services
 * Close connections
 * Check that connection pool behaves as expected as time passes
 */


#include <maxtest/testconnections.hh>

using IntVector = std::vector<int>;
void test_main(TestConnections& test);

void check_conn_pool_size(TestConnections& test, const IntVector& expected)
{
    auto& mxs = test.maxscale();
    auto info = mxs.get_servers();
    info.check_pool_connections(expected);
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    test.set_timeout(300);
    test.add_result(test.create_connections(70, true, true, true, false),
                    "Error creating connections");
    if (test.ok())
    {
        sleep(2);
        test.tprintf("Test 1:");
        IntVector expected = {1, 5, 10, 30};
        check_conn_pool_size(test, expected);

        test.tprintf("Test 2:");
        test.tprintf("Sleeping 5 seconds. Check that pool sizes have not changed...");
        sleep(5);
        check_conn_pool_size(test, expected);

        if (test.ok())
        {
            test.tprintf("Test 3:");
            test.tprintf("Sleeping 5 seconds. Check that pool of server4 is clear...");
            sleep(5);
            expected = {1, 5, 10, 0};
            check_conn_pool_size(test, expected);

            test.tprintf("Test 4:");
            test.tprintf("Sleeping 5 seconds. Check that pools of servers 2 to 4 are clear.");
            sleep(5);
            expected = {1, 0, 0, 0};
            check_conn_pool_size(test, expected);
        }
    }
}
