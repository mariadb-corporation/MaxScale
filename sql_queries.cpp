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
    int global_result = 0;
    int i;
    int N=4;

    Test->ReadEnv();
    Test->PrintIP();


    for (i = 0; i < 4; i++) {
        Test->repl->Connect();
        if (Test->ConnectMaxscale() !=0 ) {
            printf("Error connecting to MaxScale\n");
            exit(1);
        }

        global_result += insert_select(Test, N);

        printf("Creating database test1\n"); fflush(stdout);
        global_result += execute_query(Test->conn_rwsplit, "DROP TABLE t1");
        global_result += execute_query(Test->conn_rwsplit, "DROP DATABASE IF EXISTS test1;");
        global_result += execute_query(Test->conn_rwsplit, "CREATE DATABASE test1;");
        sleep(5);

        printf("Testing with database 'test1'\n");fflush(stdout);
        global_result += use_db(Test, (char *) "test1");
        global_result += insert_select(Test, N);

        global_result += check_t1_table(Test, FALSE, (char *) "test");
        global_result += check_t1_table(Test, TRUE, (char *) "test1");



        printf("Trying queries with syntax errors\n");fflush(stdout);
        execute_query(Test->conn_rwsplit, "DROP DATABASE I EXISTS test1;");
        execute_query(Test->conn_rwsplit, "CREATE TABLE ");

        execute_query(Test->conn_master, "DROP DATABASE I EXISTS test1;");
        execute_query(Test->conn_master, "CREATE TABLE ");

        execute_query(Test->conn_slave, "DROP DATABASE I EXISTS test1;");
        execute_query(Test->conn_slave, "CREATE TABLE ");

        // close connections
        Test->CloseMaxscaleConn();
        Test->repl->CloseConn();

    }

    global_result += CheckMaxscaleAlive();

    if (global_result == 0) {printf("PASSED!!\n");} else {printf("FAILED!!\n");}
    Test->Copy_all_logs(); return(global_result);
}
