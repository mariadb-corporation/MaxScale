#include <my_config.h>
#include "testconnections.h"
//#include "sql_t1.h"
//#include "get_com_select_insert.h"

typedef struct  {
    int exit_flag;
    long i;
    int rwsplit_only;
    TestConnections * Test;
} openclose_thread_data;
void *query_thread1( void *ptr );
int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);
    openclose_thread_data data;

    int threads_num = 20;

    int run_time=300;

    if (Test->smoke) {
        run_time=10;
    }

    data.i = 0;
    data.exit_flag = 0;
    data.Test = Test;
    data.rwsplit_only = 1;

    pthread_t thread1[threads_num];

    int  iret1[threads_num];

    data.exit_flag=0;
    /* Create independent threads each of them will execute function */
    for (int i = 0; i < threads_num; i++) {
        iret1[i] = pthread_create( &thread1[i], NULL, query_thread1, &data);
    }
    Test->tprintf("Threads are running %d seconds \n", run_time);
    Test->set_timeout(run_time + 20);
    sleep(run_time);

    Test->tprintf("all routers are involved, threads are running %d seconds more\n", run_time);
    Test->set_timeout(run_time + 20);
    data.rwsplit_only = 0;
    sleep(run_time);
    data.exit_flag = 1;
    sleep(5);

    Test->stop_timeout();
    Test->check_maxscale_alive();
    Test->copy_all_logs(); return(Test->global_result);
}

void *query_thread1( void *ptr )
{
    MYSQL * conn1;
    MYSQL * conn2;
    MYSQL * conn3;
    openclose_thread_data * data = (openclose_thread_data *) ptr;
    int rw_o = data->rwsplit_only;

    while (data->exit_flag == 0) {
        conn1 = data->Test->open_rwsplit_connection();
        data->Test->add_result(mysql_errno(conn1), "Error opening RWsplit conn, iteration %d, error is %s\n", data->i, mysql_error(conn1));
        if (rw_o == 0) {
            conn2 = data->Test->open_readconn_master_connection();
            data->Test->add_result(mysql_errno(conn2), "Error opening ReadConn master conn, iteration %d, error is %s\n", data->i, mysql_error(conn2));
            conn3 = data->Test->open_readconn_slave_connection();
            data->Test->add_result(mysql_errno(conn3), "Error opening ReadConn master conn, iteration %d, error is %s\n", data->i, mysql_error(conn3));
        }
        if (conn1 != NULL) {mysql_close(conn1);}
        if (rw_o == 0) {
            if (conn2 != NULL) {mysql_close(conn2);
            if (conn3 != NULL) {mysql_close(conn3);
        }
        data->i++;
    }
    return NULL;
}
