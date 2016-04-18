
/**
 * @file mxs559_block_master Playing with blocking and unblocking Master
 * It does not reproduce the bug in reliavle way, but it is a good
 * load and robustness test
 * - create load on Master RWSplit
 * - block and unblock Master in the loop
 * - repeat with different time between block/unblock
 * - check logs for lack of errors "authentication failure", "handshake failure"
 * - check for lack of crashes in the log
 */

#include <my_config.h>
#include "testconnections.h"
#include "sql_t1.h"
//#include "get_com_select_insert.h"

typedef struct  {
    int exit_flag;
    int thread_id;
    long i;
    int rwsplit_only;
    TestConnections * Test;
    MYSQL * conn1;
    MYSQL * conn2;
    MYSQL * conn3;
} openclose_thread_data;

void *disconnect_thread( void *ptr );
int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    int load_threads_num = 3;
    openclose_thread_data data_master[load_threads_num];

    int i;

    int run_time = 300;
    int iterations = 25;
    int t_iterations = 3;
    int tt[t_iterations];

    tt[0] = 1;
    tt[1] = 5;
    tt[2] = 10;

    if (Test->smoke) {
        run_time = 10;
        iterations = 5;
        t_iterations = 1;
    }

    for (i = 0; i < load_threads_num; i++) {
        data_master[i].i = 0;
        data_master[i].exit_flag = 0;
        data_master[i].Test = Test;
        data_master[i].rwsplit_only = 1;
        data_master[i].thread_id = i;
    }

    pthread_t thread_master[load_threads_num];
    int  iret_master[load_threads_num];

    Test->repl->flush_hosts();

    Test->repl->connect();
    Test->connect_maxscale();
    create_t1(Test->conn_rwsplit);
    Test->close_maxscale_connections();

    for (i = 0; i < load_threads_num; i++) { data_master[i].rwsplit_only = 1;}
    /* Create independent threads each of them will create some load on Mastet */
    for (i = 0; i < load_threads_num; i++) {
        iret_master[i] = pthread_create( &thread_master[i], NULL, disconnect_thread, &data_master[i]);
    }

    for (int j = 0; j < t_iterations; j++)
    {
        for (i = 0; i < iterations; i++)
        {
            Test->set_timeout(30);
            Test->repl->block_node(0);
            sleep(tt[j]);
            Test->set_timeout(30);
            Test->repl->unblock_node(0);
            sleep(tt[j]);
            Test->set_timeout(30);
            Test->repl->flush_hosts();
        }
    }

    Test->set_timeout(120);

    Test->tprintf("Waiting for all master load threads exit\n");
    for (i = 0; i < load_threads_num; i++)
    {
        data_master[i].exit_flag = 1;
        pthread_join(iret_master[i], NULL);
    }
    sleep(5);

    Test->tprintf("Drop t1\n");
    Test->connect_maxscale();
    Test->try_query(Test->conn_rwsplit, (char *) "DROP TABLE IF EXISTS t1;");
    Test->close_maxscale_connections();
    Test->tprintf("Checking if Maxscale alive\n");
    Test->check_maxscale_alive();
    Test->tprintf("Checking log for unwanted errors\n");
    Test->check_log_err((char *) "due to authentication failure", FALSE);
    Test->check_log_err((char *) "fatal signal 11", FALSE);
    Test->check_log_err((char *) "due to handshake failure", FALSE);
    Test->check_log_err((char *) "Refresh rate limit exceeded for load of users' table", FALSE);

    Test->copy_all_logs(); return(Test->global_result);
}


void *disconnect_thread( void *ptr )
{
    openclose_thread_data * data = (openclose_thread_data *) ptr;
    char sql[1000000];

    sleep(data->thread_id);
    create_insert_string(sql, 50000, 2);
    if (data->conn1 != NULL)
    {
        while (data->exit_flag == 0)
        {
            data->conn1 = data->Test->open_rwsplit_connection();
            execute_query_silent(data->conn1, sql);
            mysql_close(data->conn1);
            data->i++;
        }
    } else {
        data->Test->add_result(1, "Error creating MYSQL struct for Master conn\n");
    }

    return NULL;
}

