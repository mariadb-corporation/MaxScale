#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main()
{
    TestConnections * Test = new TestConnections();
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
    printf("update #1: %ld \n", (long) rows);

    execute_query_affected_rows(Test->conn_rwsplit, "UPDATE t1 SET msg='xyz' WHERE val=2", &rows);
    printf("update #2: %ld \n", (long) rows);

    execute_query_affected_rows(conn_found_rows, "UPDATE t1 SET msg='xyz' WHERE val=2", &rows);
    printf("update #3: %ld \n", (long) rows);

    Test->CloseMaxscaleConn();

    return(global_result);

}
