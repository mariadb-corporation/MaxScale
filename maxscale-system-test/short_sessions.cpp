/**
 * @file short_sessions.cpp Executes a lof of short queries, use own short session for every query (some relations to bug#424)
 *
 * - using RSplit create table
 * - close connection
 * - do 100 times: open connections to RWSplit, execute short INSERT, close connection
 * - Select inserted rows through all services
 * - check if Maxscale alive
 */


#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main(int argc, char *argv[])
{
    int iterations = 100;

    TestConnections test(argc, argv);
    test.set_timeout(20);
    test.repl->connect();

    MYSQL *conn = test.open_rwsplit_connection();
    execute_query(conn, "USE test;");
    create_t1(conn);
    mysql_close(conn);

    test.tprintf("Executing %d inserts", iterations);

    for (int i = 0; i < iterations; i++)
    {
        char sql[100];
        sprintf(sql, "INSERT INTO t1 (x1, fl) VALUES(%d, 1);", i);

        test.set_timeout(15);
        conn = test.open_rwsplit_connection();
        execute_query(conn, sql);
        mysql_close(conn);
    }

    test.set_timeout(20);
    test.add_result(test.connect_maxscale(), "Failed to connect to MaxScale");

    test.tprintf("Checking t1 table using RWSplit router");
    test.set_timeout(240);
    test.add_result(execute_select_query_and_check(test.conn_rwsplit, (char *) "SELECT * FROM t1;",
                    iterations), "t1 is wrong");

    test.tprintf("Checking t1 table using ReadConn router in master mode");
    test.set_timeout(240);
    test.add_result(execute_select_query_and_check(test.conn_master, (char *) "SELECT * FROM t1;",
                    iterations), "t1 is wrong");

    test.tprintf("Checking t1 table using ReadConn router in slave mode");
    test.set_timeout(240);
    test.add_result(execute_select_query_and_check(test.conn_slave, (char *) "SELECT * FROM t1;", iterations),
                    "t1 is wrong");

    test.set_timeout(20);
    test.close_maxscale_connections();
    test.check_maxscale_alive();

    return test.global_result;
}
