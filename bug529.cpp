/**
 * @file bug529.cpp regression case for bug 529 ( "Current no. of conns not going down" )
 *
 * - create table, opens 50 connections for every router, fill table with data using these connections.
 * - check number of connections to Master - failure if there are more then 100 connections to master.
 * - close RWSptit and ReadConn master connections and check connections to master again.
 * - create 50 ReadConn slave connection in parallel threads, execute "SELECT * FROM t1" ones for every connections, then
 * start using one of connections to create "SELECT" load.
 * - check number of connections to Master again, wait 5 seconds and check number of connections to
 * master ones more time: now expecting 0 connections to master (fail if there is a least one connection to master).
 * - close and reopens all ReadConn slave connections in the main thread and check connections to master again
 * - close all connection in all threads, close parallel thread
 * - do final 'connections to master' check
 */

// counting connection to all services

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;


pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int exit_flag = 0;
int conn_N = 50;

TestConnections * Test ;

void *parall_traffic( void *ptr );


int main(int argc, char *argv[])
{

    Test = new TestConnections(argc, argv);
    Test->set_timeout(30);
    int i;
    int num_conn = 0;
    char sql[100];

    pthread_t parall_traffic1;
    int check_iret;

    MYSQL * conn;
    MYSQL * rwsplit_conn[conn_N];
    MYSQL * master_conn[conn_N];
    MYSQL * slave_conn[conn_N];

    Test->repl->connect();

    conn = Test->open_rwsplit_connection();
    execute_query(conn, (char *) "DROP DATABASE IF EXISTS test;");
    execute_query(conn, (char *) "CREATE DATABASE test; USE test;");

    create_t1(conn);
    mysql_close(conn);
    Test->tprintf("Table t1 is created\n");

    for (i = 0; i < conn_N; i++) {
        rwsplit_conn[i] = Test->open_rwsplit_connection();
        master_conn[i] = Test->open_readconn_master_connection();
        slave_conn[i] = Test->open_readconn_slave_connection();
        sprintf(sql, "INSERT INTO t1 (x1, fl) VALUES(%d, 1);", i);
        execute_query(rwsplit_conn[i], sql);
        sprintf(sql, "INSERT INTO t1 (x1, fl) VALUES(%d, 2);", i);
        execute_query(master_conn[i], sql);
        fflush(stdout);
    }
    fflush(stdout);


    for (i = 0; i < Test->repl->N; i++) {

        num_conn = get_conn_num(Test->repl->nodes[i], Test->maxscale_IP, Test->maxscale_hostname, (char *) "test");
        Test->tprintf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn > 2*conn_N)) {
            Test->add_result(1, "too many connections to master\n");
        }
    }

    Test->tprintf("Closing RWSptit and ReadConn master connections\n");
    for (i = 0; i < conn_N; i++) {
        mysql_close(rwsplit_conn[i]);
        mysql_close(master_conn[i]);
    }

    for (i = 0; i < Test->repl->N; i++) {
        num_conn = get_conn_num(Test->repl->nodes[i], Test->maxscale_IP, Test->maxscale_hostname, (char *) "test");
        printf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn > 2*conn_N)) {
            Test->add_result(1, "too many connections to master\n");
        }
    }

    Test->tprintf("Opening more connection to ReadConn slave in parallel thread\n");

    check_iret = pthread_create( &parall_traffic1, NULL, parall_traffic, NULL);
    //pthread_join(parall_traffic1, NULL);


    for (i = 0; i < Test->repl->N; i++) {
        num_conn = get_conn_num(Test->repl->nodes[i], Test->maxscale_IP, Test->maxscale_hostname, (char *) "test");
        printf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn > 2*conn_N)) {
            Test->add_result(1, "too many connections to master\n");
        }
    }

    Test->stop_timeout();
    Test->tprintf("Sleeping 15 seconds\n");
    sleep(15);

    Test->set_timeout(30);

    for (i = 0; i < Test->repl->N; i++) {
        num_conn = get_conn_num(Test->repl->nodes[i], Test->maxscale_IP, Test->maxscale_hostname, (char *) "test");
        printf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn != 0)) {
            Test->add_result(1, "there are still connections to master\n");
        }
    }

    printf("Closing ReadConn slave connections\n");
    for (i = 0; i < conn_N; i++) {
        mysql_close(slave_conn[i]);
    }

    for (i = 0; i < Test->repl->N; i++) {
        num_conn = get_conn_num(Test->repl->nodes[i], Test->maxscale_IP, Test->maxscale_hostname, (char *) "test");
        Test->tprintf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn != 0)) {
            Test->add_result(1, "there are still connections to master\n");
        }
    }

    Test->tprintf("Opening ReadConn slave connections again\n");
    for (i = 0; i < conn_N; i++) {
        slave_conn[i] = Test->open_readconn_slave_connection();
        sprintf(sql, "SELECT * FROM t1");
        execute_query(slave_conn[i], sql);
        fflush(stdout);
    }

    for (i = 0; i < Test->repl->N; i++) {
        num_conn = get_conn_num(Test->repl->nodes[i], Test->maxscale_IP, Test->maxscale_hostname, (char *) "test");
        Test->tprintf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn != 0)) {
            Test->add_result(1, "there are still connections to master\n");
        }
    }

    Test->tprintf("Closing ReadConn slave connections\n");
    for (i = 0; i < conn_N; i++) {
        mysql_close(slave_conn[i]);
    }
    exit_flag = 1;

    for (i = 0; i < Test->repl->N; i++) {
        num_conn = get_conn_num(Test->repl->nodes[i], Test->maxscale_IP, Test->maxscale_hostname, (char *) "test");
        Test->tprintf("Connections to node %d (%s): %d\n", i, Test->repl->IP[i], num_conn);
        if ((i == 0) && (num_conn != 0)) {
            Test->add_result(1, "there are still connections to master\n");
        }
    }


    Test->copy_all_logs(); return(Test->global_result);
}


void *parall_traffic( void *ptr )
{
    MYSQL * slave_conn1[conn_N];
    int i;
    for (i = 0; i < conn_N; i++) {
        slave_conn1[i] = Test->open_readconn_slave_connection();
        execute_query(slave_conn1[i], "SELECT * FROM t1");
    }

    while (exit_flag == 0) {
        execute_query(slave_conn1[0], "SELECT * FROM t1");
    }
    for (i = 0; i < conn_N; i++) {
        mysql_close(slave_conn1[i]);
    }

    return NULL;
}
