/**
 * @file open_close_connections.cpp Simple test which creates load which is very short sessions
 *
 * - 20 threads are opening and immediatelly closing connection in the loop
 */

#include <maxtest/testconnections.hh>

#include <atomic>
#include <vector>

std::atomic<bool> run {true};

void query_thread(TestConnections& test, int thread_id)
{
    uint64_t i = 0;

    auto validate = [&](MYSQL* conn){
                        unsigned int port = 0;
                        const char* host = "<none>";
                        mariadb_get_infov(conn, MARIADB_CONNECTION_PORT, &port);
                        mariadb_get_infov(conn, MARIADB_CONNECTION_HOST, &host);

                        test.expect(mysql_errno(conn) == 0 || strstr(mysql_error(conn), "system error: 110"),
                                    "Error opening conn to %s:%u, thread num is %d, iteration %ld, error is: %s\n",
                                    host, port, thread_id, i, mysql_error(conn));

                        mysql_close(conn);
                    };

    // Keep running the test until we exhaust all available ports
    while (run && test.global_result == 0 && errno != EADDRNOTAVAIL)
    {
        validate(test.maxscales->open_rwsplit_connection());
        validate(test.maxscales->open_readconn_master_connection());
        validate(test.maxscales->open_readconn_slave_connection());
        i++;
    }
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    // Tuning these kernel parameters removes any system limitations on how many
    // connections can be created within a short period
    test.maxscales->ssh_node_f(0,
                                true,
                                "sysctl net.ipv4.tcp_tw_reuse=1 net.ipv4.tcp_tw_recycle=1 "
                                "net.core.somaxconn=10000 net.ipv4.tcp_max_syn_backlog=10000");

    test.repl->execute_query_all_nodes((char*) "set global max_connections = 50000;");
    test.repl->sync_slaves();

    std::vector<std::thread> threads;
    constexpr int threads_num = 20;

    for (int i = 0; i < threads_num; i++)
    {
        threads.emplace_back(query_thread, std::ref(test), i);
    }

    constexpr int run_time = 10;
    test.tprintf("Threads are running for %d seconds", run_time);

    for (int i = 0; i < run_time && test.global_result == 0; i++)
    {
        sleep(1);
    }

    run = false;

    for (auto& a : threads)
    {
        a.join();
    }

    test.check_maxscale_alive(0);
    return test.global_result;
}
