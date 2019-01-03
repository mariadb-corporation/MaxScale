/**
 * @file long_test.cpp Run different load for long long execution (long load test)
 *
 */


#include "testconnections.h"
#include "big_transaction.h"

typedef void * FUNC(void * ptr);

FUNC query_thread;
//void *prepared_stmt_thread(void *ptr );
FUNC transaction_thread;
FUNC short_session_thread;

TestConnections * Test;
const int threads_num = 16;
const int threads_type_num = 2;

typedef struct
{
    int id;
    bool exit_flag;
    char * sql;
} t_data;

t_data data[threads_type_num][threads_num];

int main(int argc, char *argv[])
{
    Test = new TestConnections(argc, argv);

    int i, j;

    pthread_t thread_id[threads_type_num][threads_num];
    FUNC * thread[threads_type_num];
    thread[0] = query_thread;
    //thread[1] = prepared_stmt;
    thread[1] = transaction_thread;
    thread[2] = short_session_thread;

    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 300;");
    Test->repl->execute_query_all_nodes((char *) "set global max_connect_errors = 100000;");


    Test->maxscales->connect_rwsplit(0);

    Test->repl->execute_query_all_nodes( (char *) "set global max_allowed_packet=100000000");

    create_t1(Test->maxscales->conn_rwsplit[0]);

    // Create threads
    Test->tprintf("Starting threads\n");

    for (j = 0; j < threads_type_num; j++)
    {
        for (i = 0; i < threads_num; i++)
        {
            data[j][i].sql = (char*) malloc((i +1) * 1024 * 14 + 32);
            create_insert_string(data[j][i].sql, (i + 1) * 1024 , i);
            Test->tprintf("sqL %d: %d\n", i, strlen(data[j][i].sql));
            data[j][i].exit_flag = false;
            data[j][i].id = i;
            pthread_create(&thread_id[j][i], NULL, thread[j], &data[j][i]);
        }
    }

    Test->set_log_copy_interval(300);

    sleep(600);

    Test->tprintf("Stopping threads\n");

    for (j = 0; j < threads_type_num; j++)
    {
        for (i = 0; i < threads_num; i++)
        {
            data[j][i].exit_flag = true;
            pthread_join(thread_id[j][i], NULL);
        }
    }

    Test->tprintf("Checking if MaxScale is still alive!\n");
    fflush(stdout);
    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

void *query_thread(void *ptr )
{
    t_data * data = (t_data *) ptr;
    while (!data->exit_flag)
    {
        MYSQL * conn = open_conn_db_timeout(Test->maxscales->rwsplit_port[0],
                Test->maxscales->IP[0],
                (char *) "test",
                Test->maxscales->user_name,
                Test->maxscales->password,
                20,
                Test->ssl);
        Test->try_query(conn, data->sql);
        mysql_close(conn);
    }
    return NULL;
}

void *transaction_thread(void *ptr )
{
    t_data * data = (t_data *) ptr;
    while (!data->exit_flag)
    {
        MYSQL * conn = open_conn_db_timeout(Test->maxscales->rwsplit_port[0],
                Test->maxscales->IP[0],
                (char *) "test",
                Test->maxscales->user_name,
                Test->maxscales->password,
                20,
                Test->ssl);

        Test->try_query(conn, (char *) "START TRANSACTION");
        Test->try_query(conn, (char *) "SET autocommit = 0");

        int stmt_num = 200000 / strlen(data->sql);
        for (int i = 0; i < stmt_num; i++)
        {
            Test->try_query(conn, data->sql);
        }

        Test->try_query(conn, (char *) "COMMIT");
        mysql_close(conn);
    }
    return NULL;
}

void *short_session_thread(void *ptr )
{
    t_data * data = (t_data *) ptr;
    while (!data->exit_flag)
    {
        MYSQL * conn = open_conn_db_timeout(Test->maxscales->rwsplit_port[0],
                Test->maxscales->IP[0],
                (char *) "test",
                Test->maxscales->user_name,
                Test->maxscales->password,
                20,
                Test->ssl);
        mysql_close(conn);
    }
    return NULL;
}
