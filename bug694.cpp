/**
 * @file bug694.cpp  - regression test for bug694 ("RWSplit: SELECT @a:=@a+1 as a, test.b FROM test breaks client session")
 *
 * - set use_sql_variables_in=all in MaxScale.cnf
 * - connect to readwritesplit router and execute:
 * @verbatim
CREATE TABLE test (b integer);
SELECT @a:=@a+1 as a, test.b FROM test;
USE test
@endverbatim
 * - check if MaxScale alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "mariadb_func.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->connect_maxscale();

    Test->try_query(Test->conn_rwsplit, "USE test");
    Test->try_query(Test->conn_rwsplit, "DROP TABLE IF EXISTS test");
    Test->try_query(Test->conn_rwsplit, "CREATE TABLE test (b integer)");

    const int iter = Test->smoke ? 10 : 100;
    Test->tprintf("Creating and inserting %d rows into a table\n", iter);

    for (int i=0; i < iter; i++) {
        Test->set_timeout(5);
        execute_query(Test->conn_rwsplit, "insert into test value(2);");
    }

    Test->tprintf("Trying SELECT @a:=@a+1 as a, test.b FROM test\n");
    if (execute_query(Test->conn_rwsplit, "SELECT @a:=@a+1 as a, test.b FROM test;") == 0) {
        Test->add_result(1, "Query succeded, but expected to fail.\n");
    }
    Test->tprintf("Trying USE test\n");
    Test->try_query(Test->conn_rwsplit, "USE test");

    Test->try_query(Test->conn_rwsplit, "DROP TABLE IF EXISTS test");

    Test->tprintf("Checking if MaxScale alive\n");
    Test->close_maxscale_connections();

    Test->tprintf("Checking logs\n");
    Test->check_log_err((char *) "The query can't be routed to all backend servers because it includes SELECT and SQL variable modifications which is not supported", TRUE);
    Test->check_log_err((char *) "SELECT with session data modification is not supported if configuration parameter use_sql_variables_in=all", TRUE);

    Test->check_maxscale_alive();

    Test->copy_all_logs(); return(Test->global_result);
}

