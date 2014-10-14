// bug#430

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main()
{

    TestConnections * Test = new TestConnections();
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
    execute_query(conn, "INSERT INTO t1 (x1, fl) VALUES(1, 1);");
    execute_query(conn, "INSERT INTO t1 (x1, fl) VALUES(1, 2);");

    printf("Creating temporal table t1\n");
    execute_query(conn, "create temporary table t1 as (SELECT * FROM t1 WHERE fl=3);");

    printf("Inserting one row into temporal table\n");
    execute_query(conn, "INSERT INTO t1 (x1, fl) VALUES(1, 3);");

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


    printf("Dropping temparal table and check main table again\n);");
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

    return(global_result);
}
