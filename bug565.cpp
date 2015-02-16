/**
 * @file bug565.cpp  regression case for bug 565 ( "Clients CLIENT_FOUND_ROWS setting is ignored by maxscale" )
 *
 * - open connection with CLIENT_FOUND_ROWS flag
 * - CREATE TABLE t1(id INT PRIMARY KEY, val INT, msg VARCHAR(100))
 * - INSERT INTO t1 VALUES (1, 1, 'foo'), (2, 1, 'bar'), (3, 2, 'baz'), (4, 2, 'abc')"
 * - check 'affected_rows' for folloing UPDATES:
 *   + UPDATE t1 SET msg='xyz' WHERE val=2" (expect 2)
 *   + UPDATE t1 SET msg='xyz' WHERE val=2 (expect 0)
 *   + UPDATE t1 SET msg='xyz' WHERE val=2 (expect 2)
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
    int global_result = 0;
    MYSQL * conn_found_rows;
    my_ulonglong rows;

    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();
    Test->ConnectMaxscale();

    conn_found_rows = open_conn_db_flags(Test->rwsplit_port, Test->Maxscale_IP, (char *) "test", Test->Maxscale_User, Test->Maxscale_Password, CLIENT_FOUND_ROWS);

    execute_query(Test->conn_rwsplit, "DROP TABLE IF EXISTS t1");
    execute_query(Test->conn_rwsplit, "CREATE TABLE t1(id INT PRIMARY KEY, val INT, msg VARCHAR(100))");
    execute_query(Test->conn_rwsplit, "INSERT INTO t1 VALUES (1, 1, 'foo'), (2, 1, 'bar'), (3, 2, 'baz'), (4, 2, 'abc')");

    execute_query_affected_rows(Test->conn_rwsplit, "UPDATE t1 SET msg='xyz' WHERE val=2", &rows);
    printf("update #1: %ld (expeced value is 2)\n", (long) rows);
    if (rows != 2) {global_result++;}

    execute_query_affected_rows(Test->conn_rwsplit, "UPDATE t1 SET msg='xyz' WHERE val=2", &rows);
    printf("update #2: %ld  (expeced value is 0)\n", (long) rows);
    if (rows != 0) {global_result++;}

    execute_query_affected_rows(conn_found_rows, "UPDATE t1 SET msg='xyz' WHERE val=2", &rows);
    printf("update #3: %ld  (expeced value is 2)\n", (long) rows);
    if (rows != 2) {global_result++;}

    Test->CloseMaxscaleConn();

    Test->Copy_all_logs(); return(global_result);

}
