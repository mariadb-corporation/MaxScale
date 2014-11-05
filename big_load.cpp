#include "big_load.h"

int load(int *new_inserts, int *new_selects, int *selects, int *inserts, int threads_num, TestConnections * Test, int *i1, int *i2)
{
    int global_result;
    char sql[1000000];
    thread_data data;
    Test->repl->Connect();
    Test->ConnectRWSplit();

    data.i1 = 0;
    data.i2 = 0;
    data.exit_flag = 0;
    data.Test = Test;
    // connect to the MaxScale server (rwsplit)

    if (Test->conn_rwsplit == NULL ) {
        printf("Can't connect to MaxScale\n");
        exit(1);
    } else {
        create_t1(Test->conn_rwsplit);
        create_insert_string(sql, 5000, 1);
        global_result += execute_query(Test->conn_rwsplit, sql);
        // close connections
        Test->CloseRWSplit();

        //int threads_num = 100;
        pthread_t thread1[threads_num];
        pthread_t thread2[threads_num];
        //pthread_t check_thread;
        int  iret1[threads_num];
        int  iret2[threads_num];

        printf("COM_INSERT and COM_SELECT before executing test\n");
        get_global_status_allnodes(&selects[0], &inserts[0], Test->repl, 0);
        data.exit_flag=0;
        /* Create independent threads each of them will execute function */
        for (int i = 0; i < threads_num; i++) {
            iret1[i] = pthread_create( &thread1[i], NULL, query_thread1, &data);
            iret2[i] = pthread_create( &thread2[i], NULL, query_thread2, &data);
        }
        printf("Threads are running 100 seconds \n"); fflush(stdout);
        sleep(100);
        data.exit_flag = 1;
        sleep(1);

        printf("COM_INSERT and COM_SELECT after executing test\n");
        get_global_status_allnodes(&new_selects[0], &new_inserts[0], Test->repl, 0);
        print_delta(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], Test->repl->N);
        printf("First thread did %d queries, second - %d \n", data.i1, data.i2);
    }
    Test->repl->CloseConn();
    *i1 = data.i1;
    *i2 = data.i2;
    return(global_result);
}

void *query_thread1( void *ptr )
{
    MYSQL * conn;
    thread_data * data = (thread_data *) ptr;
    conn = data->Test->OpenRWSplitConn();
    while (data->exit_flag == 0) {
        execute_query(conn, (char *) "SELECT * FROM t1;"); data->i1++;
    }
    mysql_close(conn);
    return NULL;
}

void *query_thread2(void *ptr )
{
    MYSQL * conn;
    thread_data * data = (thread_data *) ptr;
    conn = data->Test->OpenRWSplitConn();
    while (data->exit_flag == 0) {
        sleep(1);
        execute_query(conn, (char *) "SELECT * FROM t1;"); data->i2++;
    }
    mysql_close(conn);
    return NULL;
}
