/**
 * @file mxs548_short_session_change_user.cpp MXS-548 regression case ("Maxscale crash")
 * - configure 2 backend servers (one Master, one Slave)
 * - create 'user' with password 'pass2'
 * - create load on Master (3 threads are inserting data into 't1' in the loop)
 * - in 40 parallel threads open connection, execute change_user to 'user', execute change_user to default
 * user, close connection
 * - repeat test first only for RWSplit and second for all maxscales->routers[0]
 * - check logs for lack of "Unable to write to backend 'server2' due to authentication failure" errors
 * - check for lack of crashes in the log
 */


#include <maxtest/testconnections.hh>
#include <maxtest/sql_t1.hh>

#include <atomic>

std::atomic<bool> keep_running {true};

void query_thread(TestConnections& test)
{
    while (keep_running && test.ok())
    {
        std::vector<Connection> conns;
        conns.emplace_back(test.maxscales->rwsplit());
        conns.emplace_back(test.maxscales->readconn_master());
        conns.emplace_back(test.maxscales->readconn_slave());

        for (auto& conn : conns)
        {
            if (conn.connect())
            {
                test.expect(conn.change_user("user", "pass2"), "Change user to user:pass2 should work");
                test.expect(conn.reset_connection(), "Change user back should work");
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.set_timeout(20);

    test.repl->connect();
    test.maxscales->connect_maxscale(0);
    create_t1(test.maxscales->conn_rwsplit[0]);
    test.repl->execute_query_all_nodes("set global max_connections = 2000;");
    test.repl->sync_slaves();

    test.tprintf("Creating user 'user' ");
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP USER IF EXISTS user@'%%'");
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE USER user@'%%' IDENTIFIED BY 'pass2'");
    test.try_query(test.maxscales->conn_rwsplit[0], "GRANT SELECT ON test.* TO user@'%%'");
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE IF EXISTS test.t1");
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE TABLE test.t1 (x1 int, fl int)");
    test.repl->sync_slaves();

    std::vector<std::thread> threads;
    test.stop_timeout();

    for (int i = 0; i < 5; i++)
    {
        threads.emplace_back(query_thread, std::ref(test));
    }

    const int RUN_TIME = 10;
    test.tprintf("Threads are running %d seconds ", RUN_TIME);

    sleep(RUN_TIME);
    keep_running = false;

    test.set_timeout(120);
    test.tprintf("Waiting for all threads to exit");

    for (auto& a : threads)
    {
        a.join();
    }

    test.tprintf("Dropping tables and users");
    test.set_timeout(60);
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE test.t1;");
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP USER user@'%%'");
    test.maxscales->close_maxscale_connections(0);

    test.set_timeout(160);
    test.check_maxscale_alive(0);
    test.log_excludes(0, "due to authentication failure");
    test.log_excludes(0, "due to handshake failure");

    return test.global_result;
}
