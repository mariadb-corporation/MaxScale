/**
 * @file open_close_connections.cpp Simple test which creates load which is very short sessions
 *
 * - 20 threads are opening and immediatelly closing connection in the loop
 */

#include <my_config.h>
#include "testconnections.h"
//#include "sql_t1.h"
//#include "get_com_select_insert.h"

typedef struct  {
    int exit_flag;
    int thread_id;
    long i;
    int rwsplit_only;
    TestConnections * Test;
} openclose_thread_data;
void *query_thread1( void *ptr );
int threads_num = 20;
int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);


    openclose_thread_data data[threads_num];
    int i;

    int run_time=300;

    if (Test->smoke) {
        run_time=10;
    }

    for (i = 0; i < threads_num; i++) {
        data[i].i = 0;
        data[i].exit_flag = 0;
        data[i].Test = Test;
        data[i].rwsplit_only = 1;
        data[i].thread_id = i;
    }

    pthread_t thread1[threads_num];

    int  iret1[threads_num];

    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 50000;");
    Test->connect_maxscale();
    Test->try_query(Test->conn_rwsplit, (char *) "set global max_connections = 50000;");
    Test->close_maxscale_connections();

    /* Create independent threads each of them will execute function */
    for (i = 0; i < threads_num; i++) {
        iret1[i] = pthread_create( &thread1[i], NULL, query_thread1, &data[i]);
    }
    Test->tprintf("Threads are running %d seconds \n", run_time);
    for (i = 0; i < threads_num; i++) { data[i].rwsplit_only = 1;}
    Test->set_timeout(run_time + 20);
    sleep(run_time);

    Test->tprintf("all routers are involved, threads are running %d seconds more\n", run_time);
    Test->set_timeout(run_time + 40);

    for (i = 0; i < threads_num; i++) { data[i].rwsplit_only = 0;}
    sleep(run_time);
    for (i = 0; i < threads_num; i++)
    {
        data[i].exit_flag = 1;
        pthread_join(iret1[i], NULL);
    }
    sleep(5);

    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 100;");
    Test->connect_maxscale();
    Test->try_query(Test->conn_rwsplit, (char *) "set global max_connections = 100;");
    Test->close_maxscale_connections();
    Test->stop_timeout();
    Test->check_maxscale_alive();
    Test->copy_all_logs(); return(Test->global_result);
}

void *query_thread1( void *ptr )
{
    MYSQL * conn1;
    MYSQL * conn2;
    MYSQL * conn3;

    int k;

    unsigned int m_errno;
    openclose_thread_data * data = (openclose_thread_data *) ptr;
    MYSQL * conn_node[data->Test->repl->N];
    int rw_o = data->rwsplit_only;
    //sleep(data->thread_id);
    for (k = 0; k < data->Test->repl->N; k++)
    {
        conn_node[k] = open_conn(data->Test->repl->port[k],
                                 data->Test->repl->IP[k],
                                 data->Test->repl->user_name,
                                 data->Test->repl->password,
                                 data->Test->repl->ssl);
    }

    while (data->exit_flag == 0 && data->Test->global_result == 0) {
        conn1 = data->Test->open_rwsplit_connection();
        m_errno = mysql_errno(conn1);
        if (m_errno != 0)
        {
            data->Test->add_result(1, "Error opening RWsplit conn, thread num is %d, iteration %d, error is: %s\n",
                                   data->thread_id, data->i, mysql_error(conn1));
            for (k = 0; k < data->Test->repl->N; k++)
            {
                data->Test->tprintf("conn to node%d is %u (thread is is %d)\n",
                                    k,
                                    get_conn_num(conn_node[k], data->Test->maxscale_IP, data->Test->maxscale_hostname, (char *) "test"),
                                    data->thread_id);
            }
        }
        if (rw_o == 0) {
            conn2 = data->Test->open_readconn_master_connection();
            data->Test->add_result(mysql_errno(conn2), "Error opening ReadConn master conn, thread num is %d, iteration %d, error is: %s\n", data->thread_id, data->i, mysql_error(conn2));
            conn3 = data->Test->open_readconn_slave_connection();
            data->Test->add_result(mysql_errno(conn3), "Error opening ReadConn master conn, thread num is %d, iteration %d, error is: %s\n", data->thread_id, data->i, mysql_error(conn3));
        }
        /*if (data->i > 20)
        {
            data->Test->tprintf("thread %d is waiting\n", data->thread_id);
            while (get_conn_num(conn_node[0], data->Test->maxscale_IP, data->Test->maxscale_hostname, (char *) "test") > threads_num)
            {
                sleep(5);
            }
            data->Test->tprintf("thread %d continues\n", data->thread_id);
        }*/

        // USE test here is a hack to prevent Maxscale from failure; should be removed when fixed
        if (conn1 != NULL) {data->Test->try_query(conn1, (char*) "USE test");mysql_close(conn1);}
        if (rw_o == 0) {
            if (conn2 != NULL) {data->Test->try_query(conn2, (char*) "USE test");mysql_close(conn2);}
            if (conn3 != NULL) {data->Test->try_query(conn3, (char*) "USE test");mysql_close(conn3);}
        }
        data->i++; //if ((data->i / 10) * 10 == data->i) {sleep(10);}
    }
    for (k = 0; k < data->Test->repl->N; k++)
    {
        mysql_close(conn_node[k]);
    }
    return NULL;
}
