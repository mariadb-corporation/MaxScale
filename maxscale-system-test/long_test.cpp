/**
 * @file long_test.cpp Run different load for long long execution (long load test)
 *
 */


#include "testconnections.h"
#include "big_transaction.h"

typedef void * FUNC(void * ptr);

FUNC query_thread;
FUNC prepared_stmt_thread;
FUNC transaction_thread;
FUNC short_session_thread;
FUNC read_thread;

TestConnections * Test;

const int threads_type_num = 4;
int threads_num[threads_type_num];
const int max_threads_num = 32;
int port;
char * IP;

typedef struct
{
    int id;
    bool exit_flag;
    char * sql;
} t_data;

t_data data[threads_type_num][max_threads_num];

int main(int argc, char *argv[])
{
    Test = new TestConnections(argc, argv);
    int i, j;

    pthread_t thread_id[threads_type_num][max_threads_num];
    FUNC * thread[threads_type_num];
    thread[0] = query_thread;
    threads_num[0] = 1;
    thread[1] = transaction_thread;
    threads_num[1] = 1;
    thread[2] = prepared_stmt_thread;
    threads_num[2] = 1;
    thread[3] = read_thread;
    threads_num[3] = 1;

    //thread[4] = short_session_thread;
    //threads_num[4] = 4;


    port = Test->maxscales->rwsplit_port[0];
    IP = Test->maxscales->IP[0];

    //port = 3306;
    //IP = Test->repl->IP[0];


    Test->set_timeout(60);
    Test->tprintf("Set big maximums\n");

    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 300000;");
    Test->repl->execute_query_all_nodes((char *) "set global max_connect_errors = 10000000;");


    Test->maxscales->connect_rwsplit(0);

    Test->repl->execute_query_all_nodes( (char *) "set global max_allowed_packet=100000000");

    Test->tprintf("create t1 in `test` DB\n");
    create_t1(Test->maxscales->conn_rwsplit[0]);

    execute_query(Test->maxscales->conn_rwsplit[0], "DROP DATABASE test1");
    execute_query(Test->maxscales->conn_rwsplit[0], "DROP DATABASE test2");
    Test->tprintf("create`test1` DB\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "CREATE DATABASE test1");

    Test->tprintf("create`test2` DB\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "CREATE DATABASE test2");

    Test->tprintf("Waiting for slaves after DB creation\n");
    Test->repl->sync_slaves(0);
    //sleep(15);
    Test->tprintf("...ok\n");

    Test->tprintf("create t1 in `test1` DB\n");
    Test->tprintf("... use\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "USE test1");
    Test->tprintf("... create\n");
    create_t1(Test->maxscales->conn_rwsplit[0]);

    Test->tprintf("create t1 in `test2` DB\n");
    Test->tprintf("... use\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0], "USE test2");
    Test->tprintf("... create\n");
    create_t1(Test->maxscales->conn_rwsplit[0]);

    Test->tprintf("Waiting for slaves after tables creation\n");
    Test->repl->sync_slaves(0);
    //sleep(15);
    Test->tprintf("...ok\n");

    Test->set_timeout(60);
    // Create threads
    Test->tprintf("Starting threads\n");

    for (j = 0; j < threads_type_num; j++)
    {
        for (i = 0; i < threads_num[j]; i++)
        {
            data[j][i].sql = (char*) malloc((i +1) * 32 * 14 + 32);
            create_insert_string(data[j][i].sql, (i + 1) * 32 , i);
            Test->tprintf("sqL %d: %d\n", i, strlen(data[j][i].sql));
            data[j][i].exit_flag = false;
            data[j][i].id = i;
            pthread_create(&thread_id[j][i], NULL, thread[j], &data[j][i]);
        }
    }

    Test->set_log_copy_interval(100);

    Test->stop_timeout();

    char * env = getenv("long_test_time");
    int test_time = 0;
    if (env != NULL)
    {
        sscanf(env, "%d", &test_time);
    }
    if (test_time <= 0)
    {
        test_time = 3600;
        Test->tprintf("´long_test_time´ variable is not defined, set test_time to %d\n", test_time);
    }
    Test->tprintf("´test_time´ is %d\n", test_time);
    sleep(test_time);

    Test->set_timeout(180);

    Test->tprintf("Stopping threads\n");

    for (j = 0; j < threads_type_num; j++)
    {
        for (i = 0; i < threads_num[j]; i++)
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
    MYSQL * conn;
    t_data * data = (t_data *) ptr;
    int i = 0;
    conn = open_conn_db_timeout(port,
            IP,
            (char *) "test",
            Test->repl->user_name,
            Test->repl->password,
            20,
            Test->ssl);
    while (!data->exit_flag)
    {

        Test->try_query(conn, data->sql);
        i++;

    }
    mysql_close(conn);
    return NULL;
}

void *read_thread(void *ptr )
{
    MYSQL * conn;
    t_data * data = (t_data *) ptr;
    int i = 0;
    conn = open_conn_db_timeout(port,
            IP,
            (char *) "test",
            Test->repl->user_name,
            Test->repl->password,
            20,
            Test->ssl);
    while (!data->exit_flag)
    {

        Test->try_query(conn, "SELECT * FROM t1 WHERE fl=%d", data->id);
        i++;
    }
    mysql_close(conn);
    return NULL;
}

void *transaction_thread(void *ptr )
{
    MYSQL * conn;
    t_data * data = (t_data *) ptr;
    conn = open_conn_db_timeout(port,
            IP,
            (char *) "test1",
            Test->repl->user_name,
            Test->repl->password,
            20,
            Test->ssl);
    while (!data->exit_flag)
    {

        Test->try_query(conn, (char *) "START TRANSACTION");
        Test->try_query(conn, (char *) "SET autocommit = 0");

        int stmt_num = 200000 / strlen(data->sql);
        for (int i = 0; i < stmt_num; i++)
        {
            Test->try_query(conn, data->sql);
        }
        Test->try_query(conn, (char *) "COMMIT");
    }
    mysql_close(conn);

    conn = open_conn_db_timeout(port,
                    IP,
                    (char *) "",
                    Test->maxscales->user_name,
                    Test->maxscales->password,
                    20,
                    Test->ssl);
    Test->try_query(conn, "DROP DATABASE test1");
    mysql_close(conn);
    return NULL;
}

void *short_session_thread(void *ptr )
{
    MYSQL * conn;
    t_data * data = (t_data *) ptr;
    while (!data->exit_flag)
    {
        conn = open_conn_db_timeout(port,
                IP,
                (char *) "test",
                Test->repl->user_name,
                Test->repl->password,
                20,
                Test->ssl);
        mysql_close(conn);
    }
    return NULL;
}


void *prepared_stmt_thread(void *ptr )
{
    MYSQL * conn;
    t_data * data = (t_data *) ptr;
    char sql[256];
    conn = open_conn_db_timeout(port,
            IP,
            (char *) "test2",
            Test->repl->user_name,
            Test->repl->password,
            20,
            Test->ssl);
    while (!data->exit_flag)
    {
        sprintf(sql, "PREPARE stmt%d FROM 'SELECT * FROM t1 WHERE fl=@x;';", data->id);
        Test->try_query(conn, sql);
        Test->try_query(conn, "SET @x = 3;");
        sprintf(sql, "EXECUTE stmt%d", data->id);
        Test->try_query(conn, sql);
        Test->try_query(conn, "SET @x = 4;");
        Test->try_query(conn, sql);
        Test->try_query(conn, "SET @x = 400;");
        Test->try_query(conn, sql);
        sprintf(sql, "DEALLOCATE PREPARE stmt%d", data->id);
        Test->try_query(conn, sql);
    }
    mysql_close(conn);

    conn = open_conn_db_timeout(port,
                    IP,
                    (char *) "",
                    Test->maxscales->user_name,
                    Test->maxscales->password,
                    20,
                    Test->ssl);
    Test->try_query(conn, "DROP DATABASE test2");
    mysql_close(conn);
    return NULL;
}
