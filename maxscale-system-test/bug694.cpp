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

/*

Description Vilho Raatikka 2015-01-14 08:09:45 UTC
Reproduce:
- set use_sql_variables_in=all in MaxScale.cnf
- connect to readwritesplit router and execute:
CREATE TABLE test (b integer);
SELECT @a:=@a+1 as a, test.b FROM test;
USE test

You'll get:
ERROR 2006 (HY000): MySQL server has gone away

It is a known limitation that SELÈCTs with SQL variable modifications are not supported. The issue is that they aren't detected and as a consequence hte client session is disconnected.

It is possible to detect this kind of query in query classifier, but set_query_type loses part of the information. If both SELECT and SQL variable update are detected they can be stored in query type and rwsplit could, for example, prevent from executing the query, execute it in master only (and log), force all SQL variable modifications from that point to master (and log), etc.
Comment 1 Vilho Raatikka 2015-01-15 13:19:18 UTC
query_classifier.cc: set_query_type lost previous query type if the new was more restrictive. Problem was that if query is both READ and SESSION_WRITE and configuration parameter use_sql_variables_in=all was set, routing target became ambiguous. Replaced call to set_query_type with simply adding new type to type (=bit field) and checking unsupported combinations in readwritesplit.c:get_route_target. If such a case is met, a detailed error is written to error log in readwritesplit.c. mysql_client.c sees the error code and sends an error to client. Then mysql_client.c calls router's handleError which ensures that there are enough backend servers so that the session can continue.
*/


#include <iostream>
#include "testconnections.h"
#include "mariadb_func.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->set_timeout(120);
    Test->connect_maxscale();

    Test->try_query(Test->maxscales->conn_rwsplit[0], "USE test");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP TABLE IF EXISTS test");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "CREATE TABLE test (b integer)");

    const int iter = Test->smoke ? 10 : 100;
    Test->tprintf("Creating and inserting %d rows into a table\n", iter);

    for (int i = 0; i < iter; i++)
    {
        Test->set_timeout(30);
        execute_query(Test->maxscales->conn_rwsplit[0], "insert into test value(2);");
        Test->stop_timeout();
    }

    Test->set_timeout(200);

    Test->tprintf("Trying SELECT @a:=@a+1 as a, test.b FROM test\n");
    if (execute_query(Test->maxscales->conn_rwsplit[0], "SELECT @a:=@a+1 as a, test.b FROM test;") == 0)
    {
        Test->add_result(1, "Query succeded, but expected to fail.\n");
    }
    Test->tprintf("Trying USE test\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "USE test");

    Test->try_query(Test->maxscales->conn_rwsplit[0], "DROP TABLE IF EXISTS test");

    Test->tprintf("Checking if MaxScale alive\n");
    Test->close_maxscale_connections();

    Test->tprintf("Checking logs\n");
    Test->check_log_err((char *)
                        "The query can't be routed to all backend servers because it includes SELECT and SQL variable modifications which is not supported",
                        true);
    Test->check_log_err((char *)
                        "SELECT with session data modification is not supported if configuration parameter use_sql_variables_in=all",
                        true);

    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
