// some relations to bug#424

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int check_10000_inserts(MYSQL * conn) {

}

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
    execute_query(conn, (char *) "DROP DATABASE IF EXISTS test;");
    execute_query(conn, (char *) "CREATE DATABASE test; USE test;");
    create_t1(conn);
    mysql_close(conn);

    for (int i = 0; i < 10000; i++) {
        conn = Test->OpenRWSplitConn();
        sprintf(sql, "INSERT INTO t1 (x1, fl) VALUES(%d, 1);", i);
        execute_query(conn, sql);
        mysql_close(conn);
    }

    printf("Connecting to MaxScale\n");
    global_result += Test->ConnectMaxscale();
    printf("Checking t1 table using RWSplit router\n");
    global_result += execute_select_query_and_check(Test->conn_rwsplit, (char *) "SELECT * FROM t1;", 10000);
    printf("Checking t1 table using ReadConn router in master mode\n");
    global_result += execute_select_query_and_check(Test->conn_master, (char *) "SELECT * FROM t1;", 10000);
    printf("Checking t1 table using ReadConn router in slave mode\n");
    global_result += execute_select_query_and_check(Test->conn_slave, (char *) "SELECT * FROM t1;", 10000);
    Test->CloseMaxscaleConn();

    return(global_result);
}
