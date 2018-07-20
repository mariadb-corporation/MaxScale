/**
 * @file mxs564_big_dump.cpp MXS-564 regression case ("Loading database dump through readwritesplit fails")
 * - configure Maxscale to use Galera cluster
 * - start several threads which are executing session command and then sending INSERT queries agaist RWSplit router
 * - after a while block first slave
 * - after a while block second slave
 * - check that all INSERTs are ok
 * - repeat with both RWSplit and ReadConn master maxscales->routers[0]
 * - check Maxscale is alive
 */

#include "testconnections.h"
#include "sql_t1.h"

#include <atomic>
#include <thread>
#include <vector>

static std::atomic<bool> running{true};

void query_thread(TestConnections* t)
{
    TestConnections& test = *t; // For some reason CentOS 7 doesn't like passing references to std::thread
    std::string sql(1000000, '\0');
    create_insert_string(&sql[0], 1000, 2);

    MYSQL* conn1 = test.maxscales->open_rwsplit_connection();
    MYSQL* conn2 = test.maxscales->open_readconn_master_connection();

    test.add_result(mysql_errno(conn1), "Error connecting to readwritesplit: %s", mysql_error(conn1));
    test.add_result(mysql_errno(conn2), "Error connecting to readconnroute: %s", mysql_error(conn2));

    test.try_query(conn1, "SET SESSION SQL_LOG_BIN=0");
    test.try_query(conn2, "SET SESSION SQL_LOG_BIN=0");

    while (running)
    {
        test.try_query(conn1, "%s", sql.c_str());
        test.try_query(conn2, "%s", sql.c_str());
    }

    mysql_close(conn1);
    mysql_close(conn2);
}

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    int master = test.maxscales->find_master_maxadmin(test.galera);
    test.tprintf("Master: %d", master);
    std::set<int> slaves{0, 1, 2, 3};
    slaves.erase(master);

    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE IF EXISTS t1");
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE TABLE t1 (x1 int, fl int)");
    test.maxscales->disconnect();

    std::vector<std::thread> threads;

    for (int i = 0; i < 4; i++)
    {
        threads.emplace_back(query_thread, &test);
    }

    for (auto&& i : slaves)
    {
        test.tprintf("Blocking node %d", i);
        test.galera->block_node(i);
        test.maxscales->wait_for_monitor();
    }

    test.tprintf("Unblocking nodes\n");

    for (auto&& i : slaves)
    {
        test.galera->unblock_node(i);
    }

    test.maxscales->wait_for_monitor();

    running = false;
    test.set_timeout(120);
    test.tprintf("Waiting for all threads to exit");

    for (auto&& a : threads)
    {
        a.join();
    }

    test.maxscales->connect();
    execute_query(test.maxscales->conn_rwsplit[0], "DROP TABLE t1");
    test.maxscales->disconnect();

    return test.global_result;
}
