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

/**
 * @brief Creats t1 table, insert data into it and checks if data can be correctly read from all Maxscale services
 * @param Test Pointer to TestConnections object that contains references to test setup
 * @param N number of INSERTs; every next INSERT is longer 16 times in compare with previous one: for N=4 last INSERT is about 700kb long
 * @return 0 in case of no error and all checks are ok
 */
int inset_select(TestConnections* Test, int N)
{
    int global_result = 0;
    printf("Create t1\n"); fflush(stdout);
    create_t1(Test->conn_rwsplit);
    printf("Insert data into t1\n"); fflush(stdout);
    insert_into_t1(Test->conn_rwsplit, N);

    printf("SELECT: rwsplitter\n");fflush(stdout);
    global_result += select_from_t1(Test->conn_rwsplit, N);
    printf("SELECT: master\n");fflush(stdout);
    global_result += select_from_t1(Test->conn_master, N);
    printf("SELECT: slave\n");fflush(stdout);
    global_result += select_from_t1(Test->conn_slave, N);
    printf("Sleeping to let replication happen\n");fflush(stdout);
    sleep(30);
    for (int i=0; i<Test->repl->N; i++) {
        printf("SELECT: directly from node %d\n", i);fflush(stdout);
        global_result += select_from_t1(Test->repl->nodes[i], N);
    }
    return(global_result);
}

/**
 * @brief Executes USE command for all Maxscale service and all Master/Slave backend nodes
 * @param Test Pointer to TestConnections object that contains references to test setup
 * @param db Name of DB in 'USE' command
 * @return 0 in case of success
 */
int use_db(TestConnections* Test, char * db)
{
    int global_result = 0;
    char sql[100];

    sprintf(sql, "USE %s;", db);

    printf("selecting DB '%s' for rwsplit\n", db);
    global_result += execute_query(Test->conn_rwsplit, sql);
    printf("selecting DB '%s' for readconn master\n", db);
    global_result += execute_query(Test->conn_slave, sql);
    printf("selecting DB '%s' for readconn slave\n", db);
    global_result += execute_query(Test->conn_master, sql);
    for (int i = 0; i < Test->repl->N; i++) {
        printf("selecting DB '%s' for direct connection to node %d\n", db, i);
        global_result += execute_query(Test->repl->nodes[i], sql);
    }
    return(global_result);
}

/**
 * @brief Checks if table t1 exists in DB
 * @param Test Pointer to TestConnections object that contains references to test setup
 * @param presence expected result
 * @param db DB name
 * @return 0 if (t1 table exists AND presence=TRUE) OR (t1 table does not exist AND presence=FALSE)
 */

int check_t1_table(TestConnections* Test, bool presence, char * db)
{
    char * expected;
    char * actual;
    int global_result = 0;
    if (presence) {
        expected = (char *) "";
        actual   = (char *) "NOT";
    } else {
        expected = (char *) "NOT";
        actual   = (char *) "";
    }

    global_result += use_db(Test, db);

    printf("Checking: table 't1' should %s be found in '%s' database\n", expected, db);
    if ( ((check_if_t1_exists(Test->conn_rwsplit) >  0) && (!presence) ) ||
         ((check_if_t1_exists(Test->conn_rwsplit) == 0) && (presence) ))
    {global_result++; printf("Table t1 is %s found in '%s' database using RWSplit\n", actual, db); } else {
        printf("RWSplit: ok\n");
    }
    if ( ((check_if_t1_exists(Test->conn_master) >  0) && (!presence) ) ||
         ((check_if_t1_exists(Test->conn_master) == 0) && (presence) ))
    {global_result++; printf("Table t1 is %s found in '%s' database using Readconnrouter with router option master\n", actual, db); } else {
        printf("ReadConn master: ok\n");
    }
    if ( ((check_if_t1_exists(Test->conn_slave) >  0) && (!presence) ) ||
         ((check_if_t1_exists(Test->conn_slave) == 0) && (presence) ))
    {global_result++; printf("Table t1 is %s found in '%s' database using Readconnrouter with router option slave\n", actual, db); } else {
        printf("ReadConn slave: ok\n");
    }
    printf("Sleeping to let replication happen\n");
    sleep(30);
    for (int i=0; i<Test->repl->N; i++) {
        if ( ((check_if_t1_exists(Test->repl->nodes[i]) >  0) && (!presence) ) ||
             ((check_if_t1_exists(Test->repl->nodes[i]) == 0) && (presence) ))
        {global_result++; printf("Table t1 is %s found in '%s' database using direct connect to node %d\n", actual, db, i); } else {
            printf("Node %d: ok\n", i);
        }
    }
    return(global_result);
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
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

        global_result += inset_select(Test, N);

        printf("Creating database test1\n"); fflush(stdout);
        global_result += execute_query(Test->conn_rwsplit, "DROP TABLE t1");
        global_result += execute_query(Test->conn_rwsplit, "DROP DATABASE IF EXISTS test1;");
        global_result += execute_query(Test->conn_rwsplit, "CREATE DATABASE test1;");
        sleep(5);

        printf("Testing with database 'test1'\n");fflush(stdout);
        global_result += use_db(Test, (char *) "test1");
        global_result += inset_select(Test, N);

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
    exit(global_result);
}
