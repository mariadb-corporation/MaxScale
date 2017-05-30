/**
 * @file mxs548_short_session_change_user.cpp MXS-548 regression case ("Maxscale crash")
 * - configure 2 backend servers (one Master, one Slave)
 * - create 'user' with password 'pass2'
 * - create load on Master (3 threads are inserting data into 't1' in the loop)
 * - in 40 parallel threads open connection, execute change_user to 'user', execute change_user to default user, close connection
 * - repeat test first only for RWSplit and second for all routers
 * - check logs for lack of "Unable to write to backend 'server2' due to authentication failure" errors
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
void *query_thread1( void *ptr );
void *query_thread_master( void *ptr );
int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    int threads_num = 40;
    openclose_thread_data data[threads_num];

    int master_load_threads_num = 3;
    openclose_thread_data data_master[master_load_threads_num];

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


    for (i = 0; i < master_load_threads_num; i++) {
        data_master[i].i = 0;
        data_master[i].exit_flag = 0;
        data_master[i].Test = Test;
        data_master[i].rwsplit_only = 1;
        data_master[i].thread_id = i;
    }

    pthread_t thread1[threads_num];
    int  iret1[threads_num];

    pthread_t thread_master[master_load_threads_num];
    int  iret_master[master_load_threads_num];

    Test->repl->flush_hosts();

    Test->repl->connect();
    Test->connect_maxscale();
    create_t1(Test->conn_rwsplit);
    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 2000;");

    Test->tprintf("Creating user 'user' \n");

    Test->try_query(Test->conn_rwsplit, (char *) "CREATE USER user@'%%'");
    Test->try_query(Test->conn_rwsplit, (char *) "GRANT SELECT ON test.* TO user@'%%'  identified by 'pass2';  FLUSH PRIVILEGES;");
    Test->try_query(Test->conn_rwsplit, (char *) "DROP TABLE IF EXISTS t1; CREATE TABLE t1 (x1 int, fl int)");

    /* Create independent threads each of them will create some load on Mastet */
    for (i = 0; i < master_load_threads_num; i++) {
        iret_master[i] = pthread_create( &thread_master[i], NULL, query_thread_master, &data_master[i]);
    }

    /* Create independent threads each of them will execute function */
    for (i = 0; i < threads_num; i++) {
        iret1[i] = pthread_create( &thread1[i], NULL, query_thread1, &data[i]);
    }
    Test->tprintf("Threads are running %d seconds \n", run_time);
    for (i = 0; i < threads_num; i++) { data[i].rwsplit_only = 1;}
    Test->set_timeout(run_time + 60);
    sleep(run_time);

    Test->repl->flush_hosts();

    Test->tprintf("all routers are involved, threads are running %d seconds more\n", run_time);
    Test->set_timeout(run_time + 100);

    for (i = 0; i < threads_num; i++) { data[i].rwsplit_only = 0;}
    sleep(run_time);
    Test->set_timeout(120);
    Test->tprintf("Waiting for all threads exit\n");
    for (i = 0; i < threads_num; i++)
    {
        data[i].exit_flag = 1;
        pthread_join(iret1[i], NULL);
    }
    Test->tprintf("Waiting for all master load threads exit\n");
    for (i = 0; i < master_load_threads_num; i++)
    {
        data_master[i].exit_flag = 1;
        pthread_join(iret_master[i], NULL);
    }
    sleep(5);
    Test->tprintf("Flushing backend hosts\n");
    Test->set_timeout(60);
    Test->repl->flush_hosts();
    sleep(30);
    Test->set_timeout(60);
    Test->tprintf("set global max_connections = 100 for all backends\n");
    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 100;");
    Test->tprintf("Drop t1\n");
    Test->try_query(Test->conn_rwsplit, (char *) "DROP TABLE IF EXISTS t1;");
    Test->try_query(Test->conn_rwsplit, (char *) "DROP USER user@'%%'");
    Test->close_maxscale_connections();
    Test->set_timeout(160);
    Test->tprintf("Trying to connect Maxscale\n");
    Test->connect_maxscale();
    Test->tprintf("Closing Maxscale connections\n");
    Test->close_maxscale_connections();
    Test->tprintf("Checking if Maxscale alive\n");
    Test->check_maxscale_alive();
    Test->tprintf("Checking log for unwanted errors\n");
    Test->check_log_err((char *) "due to authentication failure", FALSE);
    Test->check_log_err((char *) "fatal signal 11", FALSE);
    Test->check_log_err((char *) "due to handshake failure", FALSE);
    //Test->check_log_err((char *) "Refresh rate limit exceeded for load of users' table", FALSE);

    Test->copy_all_logs(); return(Test->global_result);
}

void *query_thread1( void *ptr )
{
    openclose_thread_data * data = (openclose_thread_data *) ptr;

    while (data->exit_flag == 0) {
        data->conn1 = data->Test->open_rwsplit_connection();
        //data->conn1 = open_conn(data->Test->repl->port[0], data->Test->repl->IP[0], data->Test->repl->user_name, data->Test->repl->password, false);
        if (data->conn1 != NULL)
        {
            if ((mysql_errno(data->conn1) != 0 ) )
            {
                //data->Test->add_result(mysql_errno(data->conn1), "Error opening RWsplit conn, thread num is %d, iteration %d, error is: %s, error code is %d\n",
                //                       data->thread_id, data->i, mysql_error(data->conn1), mysql_errno(data->conn1));
            } else {
                //for (int j=0; j<10;j++) {
                /*data->Test->add_result(mysql_change_user(data->conn1, (char *) "user", (char *) "pass2", (char *) "test") , "changing user to 'user' failed, thread num is %d, iteration %d, error is: %s, error code is %d\n",
                                       data->thread_id, data->i, mysql_error(data->conn1), mysql_errno(data->conn1));
                data->Test->add_result(mysql_change_user(data->conn1, data->Test->repl->user_name, data->Test->repl->password, (char *) "test") , "changing user to '%s' failed, thread num is %d, iteration %d, error is: %s, error code is %d\n",
                                       data->Test->repl->user_name, data->thread_id, data->i, mysql_error(data->conn1), mysql_errno(data->conn1));
                //}*/
               mysql_change_user(data->conn1, (char *) "user", (char *) "pass2", (char *) "test");
               mysql_change_user(data->conn1, data->Test->repl->user_name, data->Test->repl->password, (char *) "test");
            }
        } else {
            //data->Test->add_result(1, "Error creating MYSQL struct for RWsplit conn, thread num is %d, iteration %d\n", data->thread_id, data->i);
        }
        if (data->rwsplit_only == 0) {
            data->conn2 = data->Test->open_readconn_master_connection();
            if (data->conn2 != NULL)
            {
                if ((mysql_errno(data->conn2) != 0 ) )
                {
                    //data->Test->add_result(mysql_errno(data->conn2), "Error opening ReadConn master conn, thread num is %d, iteration %d, error is: %s\n", data->thread_id, data->i, mysql_error(data->conn2));
                } else {
                    //data->Test->add_result(mysql_change_user(data->conn2, (char *) "user", (char *) "pass2", (char *) "test") , "changing user failed \n");
                    mysql_change_user(data->conn2, (char *) "user", (char *) "pass2", (char *) "test");
                    mysql_change_user(data->conn2, data->Test->repl->user_name, data->Test->repl->password, (char *) "test");
                }
            } else {
                //data->Test->add_result(1, "Error creating  MYSQL struct for ReadConn master conn, thread num is %d, iteration %d", data->thread_id, data->i);
            }
            data->conn3 = data->Test->open_readconn_slave_connection();
            if (data->conn3 != NULL)
            {
                if ((mysql_errno(data->conn3) != 0 ) ) {
                    //data->Test->add_result(mysql_errno(data->conn3), "Error opening ReadConn master conn, thread num is %d, iteration %d, error is: %s\n", data->thread_id, data->i, mysql_error(data->conn3));
                } else {
                    //data->Test->add_result(mysql_change_user(data->conn3, (char *) "user", (char *) "pass2", (char *) "test") , "changing user failed \n");
                    mysql_change_user(data->conn3, (char *) "user", (char *) "pass2", (char *) "test");
                    mysql_change_user(data->conn3, data->Test->repl->user_name, data->Test->repl->password, (char *) "test");
                }
            } else {
                //data->Test->add_result(1, "Error opening  MYSQL struct for ReadConn slave conn, thread num is %d, iteration %d\n", data->thread_id, data->i);
            }
        }
        if (data->conn1 != NULL) {mysql_close(data->conn1);}
        if (data->rwsplit_only == 0) {
            if (data->conn2 != NULL) {mysql_close(data->conn2);}
            if (data->conn3 != NULL) {mysql_close(data->conn3);}
        }
        data->i++;
    }
    return NULL;
}

void *query_thread_master( void *ptr )
{
    openclose_thread_data * data = (openclose_thread_data *) ptr;
    char sql[1000000];
    data->conn1 = open_conn(data->Test->repl->port[0], data->Test->repl->IP[0], data->Test->repl->user_name, data->Test->repl->password, false);
    create_insert_string(sql, 5000, 2);
    if (data->conn1 != NULL)
    {
        while (data->exit_flag == 0)
        {
            data->Test->try_query(data->conn1, sql);
            data->i++;
        }
    } else {
        data->Test->add_result(1, "Error creating MYSQL struct for Master conn\n");
    }

    return NULL;
}

