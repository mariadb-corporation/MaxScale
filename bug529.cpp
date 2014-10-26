// counting connection to all services

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main()
{

    TestConnections * Test = new TestConnections();
    int global_result = 0;
    int i;
    int num_conn = 0;

    Test->ReadEnv();
    Test->PrintIP();
    Test->repl->Connect();
    fflush(stdout);

    int conn_N = 50;
    MYSQL * conn;
    MYSQL * rwsplit_conn[conn_N];
    MYSQL * master_conn[conn_N];
    MYSQL * slave_conn[conn_N];
    MYSQL * slave_conn1[conn_N];

    char sql[100];

    conn = Test->OpenRWSplitConn();
    execute_query(conn, (char *) "DROP DATABASE IF EXISTS test;");
    execute_query(conn, (char *) "CREATE DATABASE test; USE test;");

    create_t1(conn);
    mysql_close(conn);
    printf("Table t1 is created\n");
    fflush(stdout);

    for (i = 0; i < conn_N; i++) {
        rwsplit_conn[i] = Test->OpenRWSplitConn();
        master_conn[i] = Test->OpenReadMasterConn();
        slave_conn[i] = Test->OpenReadSlaveConn();
        sprintf(sql, "INSERT INTO t1 (x1, fl) VALUES(%d, 1);", i);
        execute_query(rwsplit_conn[i], sql);
        sprintf(sql, "INSERT INTO t1 (x1, fl) VALUES(%d, 2);", i);
        execute_query(master_conn[i], sql);
        fflush(stdout);
    }
    fflush(stdout);

    for (i = 0; i < Test->repl->N; i++) {
        num_conn = get_conn_num(Test->repl->nodes[i], Test->Maxscale_IP, (char *) "test");
        printf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn > 2*conn_N)) {
            printf("FAILED: to many connections to master\n");
            global_result++;
        }
    }

    printf("Closing RWSptit and ReadConn master connections\n");
    for (i = 0; i < conn_N; i++) {
        mysql_close(rwsplit_conn[i]);
        mysql_close(master_conn[i]);
    }

    for (i = 0; i < Test->repl->N; i++) {
        num_conn = get_conn_num(Test->repl->nodes[i], Test->Maxscale_IP, (char *) "test");
        printf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn > 2*conn_N)) {
            printf("FAILED: to many connections to master\n");
            global_result++;
        }
    }

    printf("Opening more connection to ReadConn slave\n");

    for (i = 0; i < conn_N; i++) {
        slave_conn1[i] = Test->OpenReadSlaveConn();
        sprintf(sql, "SELECT * FROM t1");
        execute_query(slave_conn1[i], sql);

        fflush(stdout);
    }

    for (i = 0; i < Test->repl->N; i++) {
        num_conn = get_conn_num(Test->repl->nodes[i], Test->Maxscale_IP, (char *) "test");
        printf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn > 2*conn_N)) {
            printf("FAILED: to many connections to master\n");
            global_result++;
        }
    }

    printf("Sleeping 5 seconds\n");
    sleep(5);

    for (i = 0; i < Test->repl->N; i++) {
        num_conn = get_conn_num(Test->repl->nodes[i], Test->Maxscale_IP, (char *) "test");
        printf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn != 0)) {
            printf("FAILED: there are still connections to master\n");
            global_result++;
        }
    }

    printf("Closing ReadConn slave connections\n");
    for (i = 0; i < conn_N; i++) {
        mysql_close(slave_conn[i]);
    }

    for (i = 0; i < Test->repl->N; i++) {
        num_conn = get_conn_num(Test->repl->nodes[i], Test->Maxscale_IP, (char *) "test");
        printf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn != 0)) {
            printf("FAILED: there are still connections to master\n");
            global_result++;
        }
    }

    printf("Opening ReadConn slave connections again\n");
    for (i = 0; i < conn_N; i++) {
        slave_conn[i] = Test->OpenReadSlaveConn();
        sprintf(sql, "SELECT * FROM t1");
        execute_query(slave_conn[i], sql);

        fflush(stdout);
    }

    for (i = 0; i < Test->repl->N; i++) {
        num_conn = get_conn_num(Test->repl->nodes[i], Test->Maxscale_IP, (char *) "test");
        printf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn != 0)) {
            printf("FAILED: there are still connections to master\n");
            global_result++;
        }
    }

    printf("Closing ReadConn slave connections\n");
    for (i = 0; i < conn_N; i++) {
        mysql_close(slave_conn[i]);
        mysql_close(slave_conn1[i]);
    }

    for (i = 0; i < Test->repl->N; i++) {
        num_conn = get_conn_num(Test->repl->nodes[i], Test->Maxscale_IP, (char *) "test");
        printf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn != 0)) {
            printf("FAILED: there are still connections to master\n");
            global_result++;
        }
    }


    return(global_result);
}
