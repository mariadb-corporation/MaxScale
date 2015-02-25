/**
 * @file bug519.cpp
 * - fill t1 wuth data
 * - execute SELECT * INTO OUTFILE '/tmp/t1.csv' FROM t1; against all routers
 * - DROP TABLE t1
 * - LOAD DATA LOCAL INFILE 't1.csv' INTO TABLE t1; using RWSplit
 * - check if t1 contains right data
 * - SDROP t1 again and repeat LOAD DATA LOCAL INFILE 't1.csv' INTO TABLE t1; using ReadConn master
 */


#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
    int global_result = 0;
    int N=4;
    char str[1024];

    Test->ReadEnv();
    Test->PrintIP();

    Test->ConnectMaxscale();
    Test->repl->Connect();

    printf("Create t1\n"); fflush(stdout);
    create_t1(Test->conn_rwsplit);
    printf("Insert data into t1\n"); fflush(stdout);
    insert_into_t1(Test->conn_rwsplit, N);
    printf("Sleeping to let replication happen\n");fflush(stdout);
    sleep(30);

    printf("Copying data from t1 to file\n");fflush(stdout);

    sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@%s 'rm /tmp/t*.csv'", Test->repl->sshkey[0], Test->repl->IP[0]);

    printf("RWSplit:\n");fflush(stdout);
    global_result += execute_query(Test->conn_rwsplit, (char *) "SELECT * INTO OUTFILE '/tmp/t1.csv' FROM t1;");
    printf("ReadsConn master:\n");fflush(stdout);
    global_result += execute_query(Test->conn_master, (char *) "SELECT * INTO OUTFILE '/tmp/t2.csv' FROM t1;");
    printf("ReadsConn slave:\n");fflush(stdout);
    global_result += execute_query(Test->conn_slave, (char *) "SELECT * INTO OUTFILE '/tmp/t3.csv' FROM t1;");

    printf("Copying t1.cvs from Maxscale machine:\n");fflush(stdout);
    sprintf(str, "scp -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@%s:/tmp/t1.csv .", Test->repl->sshkey[0], Test->repl->IP[0]);

    system(str);

    MYSQL *srv[2];

    srv[0] = Test->conn_rwsplit;
    srv[1] = Test->conn_master;
    for (int i=0; i<2; i++) {
        printf("Dropping t1 \n");fflush(stdout);
        global_result += execute_query(Test->conn_rwsplit, (char *) "DROP TABLE t1;");
        printf("Sleeping to let replication happen\n");fflush(stdout);
        sleep(30);
        printf("Create t1\n"); fflush(stdout);
        create_t1(Test->conn_rwsplit);
        printf("Loading data to t1 from file\n");fflush(stdout);
        global_result += execute_query(srv[i], (char *) "LOAD DATA LOCAL INFILE 't1.csv' INTO TABLE t1;");

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
    }



    Test->repl->CloseConn();
    global_result += CheckMaxscaleAlive();

    Test->Copy_all_logs(); return(global_result);
}

