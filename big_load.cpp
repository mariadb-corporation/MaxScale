#include "big_load.h"

void load(long int *new_inserts, long int *new_selects, long int *selects, long int *inserts, int threads_num, TestConnections * Test, long int *i1, long int *i2, int rwsplit_only, bool galera, bool report_errors)
{
    char sql[1000000];
    thread_data data;
    Mariadb_nodes * nodes;
    if (galera) {
        nodes = Test->galera;
    } else {
        nodes = Test->repl;
    }

    int sql_l = 20000;
    int run_time=100;
    if (Test->smoke) {
        sql_l = 500;
        run_time=10;
    }

    nodes->connect();
    Test->connect_rwsplit();

    data.i1 = 0;
    data.i2 = 0;
    data.exit_flag = 0;
    data.Test = Test;
    data.rwsplit_only = rwsplit_only;
    // connect to the MaxScale server (rwsplit)

    if (Test->conn_rwsplit == NULL ) {
        if (report_errors) { Test->add_result(1, "Can't connect to MaxScale\n");}
        Test->copy_all_logs();
        exit(1);
    } else {
        create_t1(Test->conn_rwsplit);
        create_insert_string(sql, sql_l, 1);
        sleep(30);
        if ((execute_query(Test->conn_rwsplit, sql) != 0) && (report_errors)) {
            Test->add_result(1, "Query %s failed\n", sql);
        }
        // close connections
        Test->close_rwsplit();

        pthread_t thread1[threads_num];
        pthread_t thread2[threads_num];
        int  iret1[threads_num];
        int  iret2[threads_num];

        Test->tprintf("COM_INSERT and COM_SELECT before executing test\n");
        get_global_status_allnodes(&selects[0], &inserts[0], nodes, 0);
        data.exit_flag=0;
        /* Create independent threads each of them will execute function */
        for (int i = 0; i < threads_num; i++) {
            iret1[i] = pthread_create( &thread1[i], NULL, query_thread1, &data);
            iret2[i] = pthread_create( &thread2[i], NULL, query_thread2, &data);
        }
        Test->tprintf("Threads are running %d seconds \n", run_time);
        sleep(run_time);
        data.exit_flag = 1;
        sleep(1);

        Test->tprintf("COM_INSERT and COM_SELECT after executing test\n");
        get_global_status_allnodes(&new_selects[0], &new_inserts[0], nodes, 0);
        print_delta(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], nodes->N);
        Test->tprintf("First group of threads did %d queries, second - %d \n", data.i1, data.i2);
    }
    nodes->close_connections();
    *i1 = data.i1;
    *i2 = data.i2;
}

void *query_thread1( void *ptr )
{
    MYSQL * conn1;
    MYSQL * conn2;
    MYSQL * conn3;
    thread_data * data = (thread_data *) ptr;
    conn1 = data->Test->open_rwsplit_connection();
    if (data->rwsplit_only == 0) {
        conn2 = data->Test->open_readconn_master_connection();
        conn3 = data->Test->open_readconn_slave_connection();
    }
    while (data->exit_flag == 0) {
        data->Test->add_result(execute_query_silent(conn1, (char *) "SELECT * FROM t1;"), "RWSplit query failed, i=%d\n", data->i1);
        if (data->rwsplit_only == 0) {
            data->Test->add_result(execute_query_silent(conn2, (char *) "SELECT * FROM t1;"), "ReadConn master query failed, i=%d\n", data->i1);
            data->Test->add_result(execute_query_silent(conn3, (char *) "SELECT * FROM t1;"), "ReadConn slave  query failed, i=%d\n", data->i1);
        }
        data->i1++;
    }
    mysql_close(conn1);
    if (data->rwsplit_only == 0) {
        mysql_close(conn2);
        mysql_close(conn3);
    }
    return NULL;
}

void *query_thread2(void *ptr )
{
    MYSQL * conn1;
    MYSQL * conn2;
    MYSQL * conn3;
    thread_data * data = (thread_data *) ptr;
    conn1 = data->Test->open_rwsplit_connection();
    if (data->rwsplit_only == 0) {
        conn2 = data->Test->open_readconn_master_connection();
        conn3 = data->Test->open_readconn_slave_connection();
    }
    while (data->exit_flag == 0) {
        sleep(1);
        data->Test->add_result(execute_query_silent(conn1, (char *) "SELECT * FROM t1;"), "RWSplit query failed, slow thread, i=%d\n", data->i2);
        if (data->rwsplit_only == 0) {
            data->Test->add_result(execute_query_silent(conn2, (char *) "SELECT * FROM t1;"), "ReadConn master query failed, slow thread, i=%d\n", data->i2);
            data->Test->add_result(execute_query_silent(conn3, (char *) "SELECT * FROM t1;"), "ReadConn slave  query failed, slow thread,  i=%d\n", data->i2);
        }
        data->i2++;
    }
    mysql_close(conn1);
    if (data->rwsplit_only == 0) {
        mysql_close(conn2);
        mysql_close(conn3);
    }
    return NULL;
}
