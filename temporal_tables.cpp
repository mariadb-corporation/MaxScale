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

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();

    MYSQL * conn;
    char sql[100];

    conn = Test->OpenRWSplitConn();

    printf("Cleaning up DB\n");
    execute_query(conn, (char *) "DROP DATABASE IF EXISTS test;");
    execute_query(conn, (char *) "CREATE DATABASE test; USE test;");

    printf("creating table t1\n");
    create_t1(conn);

    printf("Inserting two rows into t1\n");
    execute_query(conn, "INSERT INTO t1 (x1, fl) VALUES(0, 1);");
    execute_query(conn, "INSERT INTO t1 (x1, fl) VALUES(1, 1);");

    printf("Creating temporal table t1\n");
    execute_query(conn, "create temporary table t1 as (SELECT * FROM t1 WHERE fl=3);");

    printf("Inserting one row into temporal table\n");
    execute_query(conn, "INSERT INTO t1 (x1, fl) VALUES(0, 1);");

    printf("Checking t1 temporal table\n");
    global_result += execute_select_query_and_check(conn, (char *) "SELECT * FROM t1;", 1);


    printf("Connecting to all MaxScale routers and checking main t1 table (not temporal)\n");
    global_result += Test->ConnectMaxscale();
    printf("Checking t1 table using RWSplit router\n");
    global_result += execute_select_query_and_check(Test->conn_rwsplit, (char *) "SELECT * FROM t1;", 2);
    printf("Checking t1 table using ReadConn router in master mode\n");
    global_result += execute_select_query_and_check(Test->conn_master, (char *) "SELECT * FROM t1;", 2);
    printf("Checking t1 table using ReadConn router in slave mode\n");
    global_result += execute_select_query_and_check(Test->conn_slave, (char *) "SELECT * FROM t1;", 2);
    Test->CloseMaxscaleConn();


    printf("Dropping temparal table and check main table again\n");
    execute_query(conn, "DROP TABLE t1;");

    printf("Connecting to all MaxScale routers and checking main t1 table (not temporal)\n");
    global_result += Test->ConnectMaxscale();
    printf("Checking t1 table using RWSplit router\n");
    global_result += execute_select_query_and_check(Test->conn_rwsplit, (char *) "SELECT * FROM t1;", 2);
    printf("Checking t1 table using ReadConn router in master mode\n");
    global_result += execute_select_query_and_check(Test->conn_master, (char *) "SELECT * FROM t1;", 2);
    printf("Checking t1 table using ReadConn router in slave mode\n");
    global_result += execute_select_query_and_check(Test->conn_slave, (char *) "SELECT * FROM t1;", 2);
    Test->CloseMaxscaleConn();


    mysql_close(conn);

    Test->Copy_all_logs(); return(global_result);
}
