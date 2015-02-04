
/**
 * @file sql_queries.cpp  Execute long sql queries as well as "use" command (also used for bug648 "use database is sent forever with tee filter to a readwrite split service")
 * - create t1 table and INSERT a lot of date into it
 * @verbatim
INSERT INTO t1 (x1, fl) VALUES (0, 0), (1, 0), ...(15, 0);
INSERT INTO t1 (x1, fl) VALUES (0, 1), (1, 1), ...(255, 1);
INSERT INTO t1 (x1, fl) VALUES (0, 2), (1, 2), ...(4095, 2);
INSERT INTO t1 (x1, fl) VALUES (0, 3), (1, 3), ...(65535, 3);
@endverbatim
 * - SELECT * INTO OUTFILE 't1.csv' FROM t1;
 * - DROP TABLE t1;
 * - LOAD DATA LOCAL INFILE 't1.cvs' INTO TABLE t1;
 * - check date in t1 using all Maxscale services and direct connections to backend nodes
 * -
 * - check if Maxscale is alive
 */


#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;
    int N=4;

    Test->ReadEnv();
    Test->PrintIP();

    Test->ConnectMaxscale();

    printf("Create t1\n"); fflush(stdout);
    create_t1(Test->conn_rwsplit);
    printf("Insert data into t1\n"); fflush(stdout);
    insert_into_t1(Test->conn_rwsplit, N);
    printf("Sleeping to let replication happen\n");fflush(stdout);
    sleep(30);

    printf("Copying data from t1 to file\n");fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, (char *) "SELECT * INTO OUTFILE 't1.csv' FROM t1;");

    printf("Dropping t1 \n");fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE t1;");
    printf("Sleeping to let replication happen\n");fflush(stdout);
    sleep(30);
    printf("Create t1\n"); fflush(stdout);
    create_t1(Test->conn_rwsplit);
    printf("Loading data to t1 from file\n");fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, (char *) "LOAD DATA LOCAL INFILE 't1.cvs' INTO TABLE t1;");

    printf("Sleeping to let replication happen\n");fflush(stdout);
    sleep(30);
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

    global_result += CheckMaxscaleAlive();

    return(global_result);
}

