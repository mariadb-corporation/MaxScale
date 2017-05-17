/**
 * @file temporal_tables.cpp  Check temporal tables commands functionality (relates to bug 430)
 * - create t1 table and put some data into it
 * - create tempral table t1
 * - insert different data into t1
 * - check that SELECT FROM t1 gives data from tempral table
 * - create other connections using all Maxscale services and check that SELECT via these connections gives data from main t1, not temporal
 * - dropping tempral t1
 * - check that data from main t1 is not affected
 */


#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);

    Test->repl->connect();

    MYSQL * conn;
    char sql[100];

    Test->set_timeout(40);
    conn = Test->open_rwsplit_connection();

    Test->tprintf("Cleaning up DB\n");
    execute_query(conn, (char *) "DROP DATABASE IF EXISTS test");
    execute_query(conn, (char *) "CREATE DATABASE test");
    execute_query(conn, (char *) "USE test");

    Test->tprintf("creating table t1\n");
    Test->set_timeout(40);
    create_t1(conn);

    Test->tprintf("Inserting two rows into t1\n");
    Test->set_timeout(40);
    execute_query(conn, "INSERT INTO t1 (x1, fl) VALUES(0, 1);");
    execute_query(conn, "INSERT INTO t1 (x1, fl) VALUES(1, 1);");

    Test->tprintf("Creating temporal table t1\n");
    execute_query(conn, "create temporary table t1 as (SELECT * FROM t1 WHERE fl=3);");

    Test->tprintf("Inserting one row into temporal table\n");
    execute_query(conn, "INSERT INTO t1 (x1, fl) VALUES(0, 1);");

    Test->tprintf("Checking t1 temporal table\n");
    Test->set_timeout(240);
    Test->add_result(execute_select_query_and_check(conn, (char *) "SELECT * FROM t1;", 1), "check failed\n");


    Test->tprintf("Connecting to all MaxScale routers and checking main t1 table (not temporal)\n");
    Test->set_timeout(240);
    Test->add_result(Test->connect_maxscale(), "Connectiong to Maxscale failed\n");
    Test->tprintf("Checking t1 table using RWSplit router\n");
    Test->set_timeout(240);
    Test->add_result(execute_select_query_and_check(Test->conn_rwsplit, (char *) "SELECT * FROM t1;", 2),
                     "check failed\n");
    Test->tprintf("Checking t1 table using ReadConn router in master mode\n");
    Test->set_timeout(240);
    Test->add_result(execute_select_query_and_check(Test->conn_master, (char *) "SELECT * FROM t1;", 2),
                     "check failed\n");
    Test->tprintf("Checking t1 table using ReadConn router in slave mode\n");
    Test->set_timeout(240);
    Test->add_result(execute_select_query_and_check(Test->conn_slave, (char *) "SELECT * FROM t1;", 2),
                     "check failed\n");
    Test->close_maxscale_connections();


    printf("Dropping temparal table and check main table again\n");
    execute_query(conn, "DROP TABLE t1;");

    printf("Connecting to all MaxScale routers and checking main t1 table (not temporal)\n");
    Test->add_result(Test->connect_maxscale(), "Connectiong to Maxscale failed\n");
    Test->tprintf("Checking t1 table using RWSplit router\n");
    Test->set_timeout(240);
    Test->add_result(execute_select_query_and_check(Test->conn_rwsplit, (char *) "SELECT * FROM t1;", 2),
                     "check failed\n");
    Test->tprintf("Checking t1 table using ReadConn router in master mode\n");
    Test->set_timeout(240);
    Test->add_result(execute_select_query_and_check(Test->conn_master, (char *) "SELECT * FROM t1;", 2),
                     "check failed\n");
    Test->tprintf("Checking t1 table using ReadConn router in slave mode\n");
    Test->set_timeout(240);
    Test->add_result(execute_select_query_and_check(Test->conn_slave, (char *) "SELECT * FROM t1;", 2),
                     "check failed\n");
    Test->close_maxscale_connections();

    mysql_close(conn);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
