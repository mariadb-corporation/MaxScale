/**
 * @file pers_01.cpp - Persistent connection test
 * Open 70 connections to all Maxscale services
 * Close connections
 * Check that connection pool behaves as expected as time passes
 */


#include <maxtest/testconnections.hh>
#include <maxtest/mariadb_connector.hh>

using IntVector = std::vector<int>;
void test_main(TestConnections& test);

void check_conn_pool_size(TestConnections& test, const IntVector& expected);
int  get_server_conn_count(TestConnections& test);

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

    // Test pre-emptive pooling.
    if (test.ok())
    {
        test.set_timeout(60);

        // First, check idle connection count. Restart MaxScale to get rid of any previously pooled
        // connections.
        auto& mxs = test.maxscale->maxscale_b();
        mxs.stop();
        mxs.start();
        sleep(2);
        int idle_conn_count = get_server_conn_count(test);

        auto generate_conns = [&mxs](int n_conns) {
                std::vector<std::unique_ptr<mxt::MariaDB>> conns;
                for (int i = 0; i < n_conns; i++)
                {
                    conns.push_back(mxs.open_rwsplit_connection());
                }
                return conns;
            };

        auto expect_conn_count = [&test](int found, int expected) {
                test.tprintf("Backend connection count is %i", found);
                test.expect(found == expected, "Unexpected number of connections: got %i, expected %i",
                            found, expected);
            };
        int n_conns = 20;

        // Add connections without pooling. This is just to ensure connection count is predictable.
        auto conns_1A = generate_conns(n_conns);
        int conn_count_1A = get_server_conn_count(test);
        expect_conn_count(conn_count_1A, idle_conn_count + n_conns);

        auto conns_1B = generate_conns(n_conns);
        int conn_count_1B = get_server_conn_count(test);
        expect_conn_count(conn_count_1B, idle_conn_count + 2 * n_conns);

        // Close all connections.
        conns_1A.clear();
        conns_1B.clear();
        sleep(1);

        int conn_count_1C = get_server_conn_count(test);
        expect_conn_count(conn_count_1C, idle_conn_count + 1);      // +1 because one left in pool

        if (test.ok())
        {
            // Set up pooling. Again restart to reset pool.
            mxs.stop();
            mxs.start();
            sleep(2);

            mxs.alter_server("server1", "persistpoolmax", "100");
            mxs.alter_service("RW-Split-Router", "idle_session_pool_time", "5s");
            test.tprintf("Idle session pooling enabled.");

            // Generate connections, then wait for MaxScale to pool them.
            auto conns_2A = generate_conns(n_conns);
            int conn_count_2A = get_server_conn_count(test);
            expect_conn_count(conn_count_2A, idle_conn_count + n_conns);

            test.tprintf("%i connections to backend, waiting for MaxScale to pool them.", conn_count_2A);
            sleep(8);
            check_conn_pool_size(test, {n_conns});

            // Generate more connections. This should not increase count on backend as much as before,
            // since most connections can be taken from pool. Due to uneven distribution of connections
            // in thread-specific pools, the end result is not exact.
            auto conns_2B = generate_conns(n_conns);
            int conn_count_2B = get_server_conn_count(test);
            int increase = conn_count_2B - conn_count_2A;
            test.tprintf("Generated %i new connections to MaxScale, which resulted in %i more connections "
                         "to backend.", n_conns, increase);
            int expected_increase_max = n_conns / 2;    // This depends on how connections divide to threads.
            test.expect(increase < expected_increase_max, "Got %i new connections when at most %i was "
                                                          "expected", increase, expected_increase_max);
        }
    }
}

void check_conn_pool_size(TestConnections& test, const IntVector& expected)
{
    auto& mxs = test.maxscale->maxscale_b();
    auto info = mxs.get_servers();
    info.check_pool_connections(expected);
}

int get_server_conn_count(TestConnections& test)
{
    bool success = false;
    int rval = -1;
    auto conn = test.repl->get_connection(0);
    if (conn.connect())
    {
        auto res = conn.row("select count(*) from information_schema.PROCESSLIST "
                            "where user != 'system user' and ID != CONNECTION_ID();");
        if (!res.empty())
        {
            if (mxb::get_int(res[0].c_str(), &rval))
            {
                success = true;
            }
        }
    }
    test.expect(success, "Failed to read master server process count");
    return rval;
}
