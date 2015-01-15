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

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;

    Test->ReadEnv();
    Test->PrintIP();

    Test->ConnectMaxscale();

    printf("Trying SELECT @a:=@a+1 as a, test.b FROM test\n"); fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, "CREATE TABLE test (b integer);");
    global_result += execute_query(Test->conn_rwsplit, "SELECT @a:=@a+1 as a, test.b FROM test;");
    printf("Trying USE test"); fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, "USE test");

    Test->CloseMaxscaleConn();

    //global_result    += CheckLogErr((char *) "Error : Unable to start RW Split Router service. There are too few backend servers configured in MaxScale.cnf. Found 10% when at least 33% would be required", TRUE);

    global_result += CheckMaxscaleAlive();

    return(global_result);
}

