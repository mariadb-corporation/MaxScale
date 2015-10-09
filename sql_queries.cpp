/**
 * @file sql_queries.cpp  Execute long sql queries as well as "use" command (also used for bug648 "use database is sent forever with tee filter to a readwrite split service")
 * - for bug648:
 * @verbatim
[RW Split Router]
type=service
router= readwritesplit
servers=server1,     server2,              server3,server4
user=skysql
passwd=skysql
filters=TEE

[TEE]
type=filter
module=tee
service=RW Split Router
@endverbatim
 * - create t1 table and INSERT a lot of date into it
 * @verbatim
INSERT INTO t1 (x1, fl) VALUES (0, 0), (1, 0), ...(15, 0);
INSERT INTO t1 (x1, fl) VALUES (0, 1), (1, 1), ...(255, 1);
INSERT INTO t1 (x1, fl) VALUES (0, 2), (1, 2), ...(4095, 2);
INSERT INTO t1 (x1, fl) VALUES (0, 3), (1, 3), ...(65535, 3);
@endverbatim
 * - check date in t1 using all Maxscale services and direct connections to backend nodes
 * - using RWSplit connections:
 *   + DROP TABLE t1
 *   + DROP DATABASE IF EXISTS test1;
 *   + CREATE DATABASE test1;
 * - execute USE test1 for all Maxscale service and backend nodes
 * - create t1 table and INSERT a lot of date into it
 * - check that 't1' exists in 'test1' DB and does not exist in 'test'
 * - executes queries with syntax error against all Maxscale services
 *   + "DROP DATABASE I EXISTS test1;"
 *   + "CREATE TABLE "
 * - check if Maxscale is alive
 */


#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;


int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int i;
    int N=4;

    Test->tprintf("Starting test\n");
    for (i = 0; i < 4; i++) {
        Test->tprintf("Connection to backend\n");
        Test->set_timeout(5);
        Test->repl->connect();
        Test->tprintf("Connection to Maxscale\n");
        if (Test->connect_maxscale() !=0 ) {
            Test->tprintf("Error connecting to MaxScale\n");
            Test->copy_all_logs();
            exit(1);
        }

        Test->tprintf("Filling t1 with data\n");
        Test->set_timeout(100);
        Test->add_result(Test->insert_select(N), "insert-select check failed\n");

        Test->tprintf("Creating database test1\n");
        Test->try_query(Test->conn_rwsplit, "DROP TABLE t1");
        Test->try_query(Test->conn_rwsplit, "DROP DATABASE IF EXISTS test1;");
        Test->try_query(Test->conn_rwsplit, "CREATE DATABASE test1;");
        Test->stop_timeout();
        sleep(5);

        Test->set_timeout(1000);
        Test->tprintf("Testing with database 'test1'\n");
        Test->add_result(Test->use_db( (char *) "test1"), "use_db failed\n");
        Test->add_result(Test->insert_select(N), "insert-select check failed\n");
        Test->stop_timeout();

        Test->set_timeout(5);
        Test->add_result(Test->check_t1_table(FALSE, (char *) "test"), "t1 is found in 'test'\n");
        Test->add_result(Test->check_t1_table(TRUE, (char *) "test1"), "t1 is not found in 'test1'\n");

        Test->tprintf("Trying queries with syntax errors\n");
        execute_query(Test->conn_rwsplit, "DROP DATABASE I EXISTS test1;");
        execute_query(Test->conn_rwsplit, "CREATE TABLE ");

        execute_query(Test->conn_master, "DROP DATABASE I EXISTS test1;");
        execute_query(Test->conn_master, "CREATE TABLE ");

        execute_query(Test->conn_slave, "DROP DATABASE I EXISTS test1;");
        execute_query(Test->conn_slave, "CREATE TABLE ");

        // close connections
        Test->close_maxscale_connections();
        Test->repl->close_connections();
        Test->stop_timeout();

    }

    Test->set_timeout(5);
    Test->check_maxscale_alive();

    Test->copy_all_logs(); return(Test->global_result);
}
