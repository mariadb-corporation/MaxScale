/**
 * @file open_close_connections.cpp Simple test which creates load which is very short sessions
 *
 * - 20 threads are opening and immediatelly closing connection in the loop
 */

#include "testconnections.h"

typedef struct
{
    int exit_flag;
    int thread_id;
    long i;
    int rwsplit_only;
    TestConnections * Test;
} openclose_thread_data;

void *query_thread1(void *ptr);
int threads_num = 20;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int run_time = Test->smoke ? 10 : 300;

    openclose_thread_data data[threads_num];
    for (int i = 0; i < threads_num; i++)
    {
        data[i].i = 0;
        data[i].exit_flag = 0;
        data[i].Test = Test;
        data[i].thread_id = i;
    }

    // Tuning these kernel parameters removes any system limitations on how many
    // connections can be created within a short period
    Test->maxscales->ssh_node_f(0, true, "sysctl net.ipv4.tcp_tw_reuse=1 net.ipv4.tcp_tw_recycle=1 "
                                "net.core.somaxconn=10000 net.ipv4.tcp_max_syn_backlog=10000");

    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 50000;");
    Test->repl->sync_slaves();

    pthread_t thread1[threads_num];

    /* Create independent threads each of them will execute function */
    for (int i = 0; i < threads_num; i++)
    {
        pthread_create(&thread1[i], NULL, query_thread1, &data[i]);
    }

    Test->tprintf("Threads are running %d seconds \n", run_time);

    for (int i = 0; i < run_time && Test->global_result == 0; i++)
    {
        sleep(1);
    }

    for (int i = 0; i < threads_num; i++)
    {
        data[i].exit_flag = 1;
        pthread_join(thread1[i], NULL);
    }

    Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}

void *query_thread1( void *ptr )
{
    openclose_thread_data * data = (openclose_thread_data *) ptr;

    while (data->exit_flag == 0 && data->Test->global_result == 0)
    {
        MYSQL *conn1 = data->Test->maxscales->open_rwsplit_connection(0);
        data->Test->add_result(mysql_errno(conn1),
                               "Error opening RWsplit conn, thread num is %d, iteration %d, error is: %s\n",
                               data->thread_id, data->i, mysql_error(conn1));
        MYSQL *conn2 = data->Test->maxscales->open_readconn_master_connection(0);
        data->Test->add_result(mysql_errno(conn2),
                               "Error opening ReadConn master conn, thread num is %d, iteration %d, error is: %s\n", data->thread_id,
                               data->i, mysql_error(conn2));
        MYSQL *conn3 = data->Test->maxscales->open_readconn_slave_connection(0);
        data->Test->add_result(mysql_errno(conn3),
                               "Error opening ReadConn master conn, thread num is %d, iteration %d, error is: %s\n", data->thread_id,
                               data->i, mysql_error(conn3));
        // USE test here is a hack to prevent Maxscale from failure; should be removed when fixed
        if (conn1 != NULL)
        {
            data->Test->try_query(conn1, (char*) "USE test");
            mysql_close(conn1);
        }

        if (conn2 != NULL)
        {
            data->Test->try_query(conn2, (char*) "USE test");
            mysql_close(conn2);
        }
        if (conn3 != NULL)
        {
            data->Test->try_query(conn3, (char*) "USE test");
            mysql_close(conn3);
        }

        data->i++;
    }

    return NULL;
}
