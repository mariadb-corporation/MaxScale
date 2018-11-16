/**
 * @file mxs548_short_session_change_user.cpp MXS-548 regression case ("Maxscale crash")
 * - configure 2 backend servers (one Master, one Slave)
 * - create 'user' with password 'pass2'
 * - create load on Master (3 threads are inserting data into 't1' in the loop)
 * - in 40 parallel threads open connection, execute change_user to 'user', execute change_user to default
 * user, close connection
 * - repeat test first only for RWSplit and second for all maxscales->routers[0]
 * - check logs for lack of "Unable to write to backend 'server2' due to authentication failure" errors
 * - check for lack of crashes in the log
 */


#include "testconnections.h"
#include "sql_t1.h"

typedef struct
{
    int              exit_flag;
    int              thread_id;
    long             i;
    int              rwsplit_only;
    TestConnections* Test;
    MYSQL*           conn1;
    MYSQL*           conn2;
    MYSQL*           conn3;
} openclose_thread_data;

void* query_thread1(void* ptr);
void* query_thread_master(void* ptr);

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->maxscales->ssh_node_f(0,
                                true,
                                "sysctl net.ipv4.tcp_tw_reuse=1 net.ipv4.tcp_tw_recycle=1 "
                                "net.core.somaxconn=10000 net.ipv4.tcp_max_syn_backlog=10000");
    Test->set_timeout(20);

    int threads_num = 40;
    openclose_thread_data data[threads_num];

    int master_load_threads_num = 3;
    openclose_thread_data data_master[master_load_threads_num];

    int i;

    int run_time = 300;

    if (Test->smoke)
    {
        run_time = 10;
    }

    for (i = 0; i < threads_num; i++)
    {
        data[i].i = 0;
        data[i].exit_flag = 0;
        data[i].Test = Test;
        data[i].rwsplit_only = 1;
        data[i].thread_id = i;
        data[i].conn1 = NULL;
        data[i].conn2 = NULL;
        data[i].conn3 = NULL;
    }


    for (i = 0; i < master_load_threads_num; i++)
    {
        data_master[i].i = 0;
        data_master[i].exit_flag = 0;
        data_master[i].Test = Test;
        data_master[i].rwsplit_only = 1;
        data_master[i].thread_id = i;
        data_master[i].conn1 = NULL;
        data_master[i].conn2 = NULL;
        data_master[i].conn3 = NULL;
    }

    pthread_t thread1[threads_num];

    pthread_t thread_master[master_load_threads_num];

    Test->repl->connect();
    Test->maxscales->connect_maxscale(0);
    create_t1(Test->maxscales->conn_rwsplit[0]);
    Test->repl->execute_query_all_nodes((char*) "set global max_connections = 2000;");
    Test->repl->sync_slaves();

    Test->tprintf("Creating user 'user' \n");
    execute_query(Test->maxscales->conn_rwsplit[0], (char*) "DROP USER IF EXISTS user@'%%'");
    execute_query(Test->maxscales->conn_rwsplit[0], (char*) "CREATE USER user@'%%' IDENTIFIED BY 'pass2'");
    execute_query(Test->maxscales->conn_rwsplit[0], (char*) "GRANT SELECT ON test.* TO user@'%%'");
    execute_query(Test->maxscales->conn_rwsplit[0], (char*) "DROP TABLE IF EXISTS test.t1");
    execute_query(Test->maxscales->conn_rwsplit[0], (char*) "CREATE TABLE test.t1 (x1 int, fl int)");
    Test->repl->sync_slaves();

    /* Create independent threads each of them will create some load on Master */
    for (i = 0; i < master_load_threads_num; i++)
    {
        pthread_create(&thread_master[i], NULL, query_thread_master, &data_master[i]);
    }

    /* Create independent threads each of them will execute function */
    for (i = 0; i < threads_num; i++)
    {
        pthread_create(&thread1[i], NULL, query_thread1, &data[i]);
    }

    Test->tprintf("Threads are running %d seconds \n", run_time);

    for (i = 0; i < threads_num; i++)
    {
        data[i].rwsplit_only = 1;
    }

    Test->set_timeout(run_time + 60);
    sleep(run_time);

    Test->repl->flush_hosts();

    Test->tprintf("all maxscales->routers[0] are involved, threads are running %d seconds more\n", run_time);
    Test->set_timeout(run_time + 100);

    for (i = 0; i < threads_num; i++)
    {
        data[i].rwsplit_only = 0;
    }

    sleep(run_time);
    Test->set_timeout(120);
    Test->tprintf("Waiting for all threads exit\n");

    for (i = 0; i < threads_num; i++)
    {
        data[i].exit_flag = 1;
        pthread_join(thread1[i], NULL);
    }

    Test->tprintf("Waiting for all master load threads exit\n");

    for (i = 0; i < master_load_threads_num; i++)
    {
        data_master[i].exit_flag = 1;
        pthread_join(thread_master[i], NULL);
    }

    Test->tprintf("Flushing backend hosts\n");
    Test->set_timeout(60);
    Test->repl->flush_hosts();

    Test->tprintf("Dropping tables and users\n");
    Test->set_timeout(60);
    execute_query(Test->maxscales->conn_rwsplit[0], (char*) "DROP TABLE test.t1;");
    execute_query(Test->maxscales->conn_rwsplit[0], (char*) "DROP USER user@'%%'");
    Test->maxscales->close_maxscale_connections(0);

    Test->set_timeout(160);
    Test->tprintf("Trying to connect Maxscale\n");
    Test->maxscales->connect_maxscale(0);
    Test->tprintf("Closing Maxscale connections\n");
    Test->maxscales->close_maxscale_connections(0);
    Test->tprintf("Checking if Maxscale alive\n");
    Test->check_maxscale_alive(0);
    Test->tprintf("Checking log for unwanted errors\n");
    Test->log_excludes(0, "due to authentication failure");
    Test->log_excludes(0, "due to handshake failure");

    // We need to wait for the TCP connections in TIME_WAIT state so that
    // later tests don't fail due to a lack of file descriptors
    Test->tprintf("Waiting for network connections to die");
    sleep(30);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

void* query_thread1(void* ptr)
{
    openclose_thread_data* data = (openclose_thread_data*) ptr;

    while (data->exit_flag == 0)
    {
        data->conn1 = data->Test->maxscales->open_rwsplit_connection(0);

        if (data->conn1 != NULL)
        {
            if (mysql_errno(data->conn1) == 0)
            {
                mysql_change_user(data->conn1, (char*) "user", (char*) "pass2", (char*) "test");
                mysql_change_user(data->conn1,
                                  data->Test->repl->user_name,
                                  data->Test->repl->password,
                                  (char*) "test");
            }
        }
        if (data->rwsplit_only == 0)
        {
            data->conn2 = data->Test->maxscales->open_readconn_master_connection(0);

            if (data->conn2 != NULL)
            {
                if (mysql_errno(data->conn2) == 0)
                {
                    mysql_change_user(data->conn2, (char*) "user", (char*) "pass2", (char*) "test");
                    mysql_change_user(data->conn2,
                                      data->Test->repl->user_name,
                                      data->Test->repl->password,
                                      (char*) "test");
                }
            }

            data->conn3 = data->Test->maxscales->open_readconn_slave_connection(0);

            if (data->conn3 != NULL)
            {
                if (mysql_errno(data->conn3) == 0)
                {
                    mysql_change_user(data->conn3, (char*) "user", (char*) "pass2", (char*) "test");
                    mysql_change_user(data->conn3,
                                      data->Test->repl->user_name,
                                      data->Test->repl->password,
                                      (char*) "test");
                }
            }
        }

        if (data->conn1 != NULL)
        {
            mysql_close(data->conn1);
            data->conn1 = NULL;
        }

        if (data->rwsplit_only == 0)
        {
            if (data->conn2 != NULL)
            {
                mysql_close(data->conn2);
                data->conn2 = NULL;
            }

            if (data->conn3 != NULL)
            {
                mysql_close(data->conn3);
                data->conn3 = NULL;
            }
        }

        data->i++;
    }

    return NULL;
}

void* query_thread_master(void* ptr)
{
    openclose_thread_data* data = (openclose_thread_data*) ptr;
    char sql[1000000];
    data->conn1 = open_conn(data->Test->repl->port[0],
                            data->Test->repl->IP[0],
                            data->Test->repl->user_name,
                            data->Test->repl->password,
                            false);
    create_insert_string(sql, 5000, 2);

    if (data->conn1 != NULL)
    {
        while (data->exit_flag == 0)
        {
            data->Test->try_query(data->conn1, "%s", sql);
            data->i++;
        }
    }
    else
    {
        data->Test->add_result(1, "Error creating MYSQL struct for Master conn\n");
    }

    return NULL;
}
