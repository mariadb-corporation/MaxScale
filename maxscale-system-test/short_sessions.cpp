/**
 * @file short_sessions.cpp Executes a lof of short queries, use own short session for every query (some relations to bug#424)
 *
 * - using RSplit create table
 * - close connection
 * - do 10000 times: open connections to RWSplit, execute short INSERT, close connection
 * - do 10000 times: open connection to RWSplit, execute short SELECT, close connection
 * - repeat last previous step also to ReadConn master and ReadConn slave
 * - check if Maxscale alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main(int argc, char *argv[])
{
    int iterations = 10000;

    TestConnections * Test = new TestConnections(argc, argv);
    if (Test->smoke) {iterations = 100;}
    Test->set_timeout(20);

    Test->repl->connect();

    MYSQL * conn;
    char sql[100];

    conn = Test->open_rwsplit_connection();
    execute_query(conn, (char *) "DROP DATABASE IF EXISTS test;");
    execute_query(conn, (char *) "CREATE DATABASE test; USE test;");
    execute_query(conn, (char *) "USE test_non_existing_DB; USE test;");
    create_t1(conn);
    mysql_close(conn);
    Test->tprintf("Table t1 is created\n");

    for (int i = 0; i < iterations; i++) {
        Test->set_timeout(15);
        conn = Test->open_rwsplit_connection();
        sprintf(sql, "INSERT INTO t1 (x1, fl) VALUES(%d, 1);", i);
        Test->tprintf("%s\n", sql);
        execute_query(conn, sql);
        mysql_close(conn);
    }
    Test->set_timeout(20);
    fflush(stdout);

    Test->set_timeout(20);
    Test->tprintf("Connecting to MaxScale\n");
    Test->add_result(Test->connect_maxscale(), "Error connecting to Maxscale\n");
    Test->tprintf("Checking t1 table using RWSplit router\n");
    Test->set_timeout(240);
    Test->add_result( execute_select_query_and_check(Test->conn_rwsplit, (char *) "SELECT * FROM t1;", iterations), "t1 is wrong\n");
    Test->tprintf("Checking t1 table using ReadConn router in master mode\n");
    Test->set_timeout(240);
    Test->add_result(  execute_select_query_and_check(Test->conn_master, (char *) "SELECT * FROM t1;", iterations), "t1 is wrong\n");
    Test->tprintf("Checking t1 table using ReadConn router in slave mode\n");
    Test->set_timeout(240);
    Test->add_result(  execute_select_query_and_check(Test->conn_slave, (char *) "SELECT * FROM t1;", iterations), "t1 is wrong\n");
    Test->set_timeout(20);
    Test->close_maxscale_connections();

    Test->check_maxscale_alive();

    Test->copy_all_logs(); return(Test->global_result);
}
