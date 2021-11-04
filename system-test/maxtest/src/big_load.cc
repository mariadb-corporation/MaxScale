#include <maxtest/big_load.hh>

#include <pthread.h>
#include <maxtest/testconnections.hh>
#include <maxtest/sql_t1.hh>
#include <maxtest/get_com_select_insert.hh>
#include <maxtest/galera_cluster.hh>
#include <maxtest/replication_cluster.hh>

void load(long int* new_inserts,
          long int* new_selects,
          long int* selects,
          long int* inserts,
          int threads_num,
          TestConnections* Test,
          long int* i1,
          long int* i2,
          int rwsplit_only,
          bool galera,
          bool report_errors)
{
    char sql[1000000];
    thread_data data;
    MariaDBCluster* nodes;
    if (galera)
    {
        nodes = Test->galera;
    }
    else
    {
        nodes = Test->repl;
    }

    int sql_l = 20000;
    int run_time = 100;
    if (Test->smoke)
    {
        sql_l = 500;
        run_time = 10;
    }

    nodes->connect();
    Test->maxscale->connect_rwsplit();

    data.i1 = 0;
    data.i2 = 0;
    data.exit_flag = 0;
    data.Test = Test;
    data.rwsplit_only = rwsplit_only;
    // connect to the MaxScale server (rwsplit)

    if (Test->maxscale->conn_rwsplit == NULL)
    {
        if (report_errors)
        {
            Test->add_result(1, "Can't connect to MaxScale\n");
        }
        // Test->copy_all_logs();
        exit(1);
    }
    else
    {
        create_t1(Test->maxscale->conn_rwsplit);
        create_insert_string(sql, sql_l, 1);

        if ((execute_query(Test->maxscale->conn_rwsplit, "%s", sql) != 0) && (report_errors))
        {
            Test->add_result(1, "Query %s failed\n", sql);
        }
        // close connections
        Test->maxscale->close_rwsplit();

        if (nodes == Test->repl)
        {
            Test->tprintf("Waiting for the table to replicate\n");
            Test->repl->sync_slaves();
        }

        pthread_t thread1[threads_num];
        pthread_t thread2[threads_num];

        Test->tprintf("COM_INSERT and COM_SELECT before executing test\n");

        Test->add_result(get_global_status_allnodes(&selects[0], &inserts[0], nodes, 0),
                         "get_global_status_allnodes failed\n");

        data.exit_flag = 0;
        /* Create independent threads each of them will execute function */
        for (int i = 0; i < threads_num; i++)
        {
            pthread_create(&thread1[i], NULL, query_thread1, &data);
            pthread_create(&thread2[i], NULL, query_thread2, &data);
        }
        Test->tprintf("Threads are running %d seconds \n", run_time);
        sleep(run_time);
        data.exit_flag = 1;
        Test->tprintf("Waiting for all threads to exit\n");

        for (int i = 0; i < threads_num; i++)
        {
            pthread_join(thread1[i], NULL);
            pthread_join(thread2[i], NULL);
        }
        sleep(1);

        Test->tprintf("COM_INSERT and COM_SELECT after executing test\n");
        get_global_status_allnodes(&new_selects[0], &new_inserts[0], nodes, 0);
        print_delta(&new_selects[0], &new_inserts[0], &selects[0], &inserts[0], nodes->N);
        Test->tprintf("First group of threads did %ld queries, second - %ld \n", data.i1, data.i2);
    }
    nodes->close_connections();
    *i1 = data.i1;
    *i2 = data.i2;
}

void* query_thread1(void* ptr)
{
    MYSQL* conn1 = nullptr;
    MYSQL* conn2 = nullptr;
    MYSQL* conn3 = nullptr;
    int conn_err = 0;
    thread_data* data = (thread_data*) ptr;
    auto mxs_ip = data->Test->maxscale->ip4();
    auto mxs_user = data->Test->maxscale->user_name();
    auto mxs_pw = data->Test->maxscale->password();

    conn1 = open_conn_db_timeout(data->Test->maxscale->rwsplit_port, mxs_ip,
                                 "test",
                                 mxs_user,
                                 mxs_pw,
                                 20,
                                 data->Test->maxscale_ssl);
    // conn1 = data->Test->maxscales->open_rwsplit_connection(0);
    if (mysql_errno(conn1) != 0)
    {
        conn_err++;
    }
    if (data->rwsplit_only == 0)
    {
        // conn2 = data->Test->maxscales->open_readconn_master_connection(0);
        conn2 = open_conn_db_timeout(data->Test->maxscale->readconn_master_port, mxs_ip,
                                     "test",
                                     mxs_user,
                                     mxs_pw,
                                     20,
                                     data->Test->maxscale_ssl);
        if (mysql_errno(conn2) != 0)
        {
            conn_err++;
        }
        // conn3 = data->Test->maxscales->open_readconn_slave_connection(0);
        conn3 = open_conn_db_timeout(data->Test->maxscale->readconn_slave_port, mxs_ip,
                                     "test",
                                     mxs_user,
                                     mxs_pw,
                                     20,
                                     data->Test->maxscale_ssl);
        if (mysql_errno(conn3) != 0)
        {
            conn_err++;
        }
    }
    if (conn_err == 0)
    {
        while (data->exit_flag == 0)
        {
            if (execute_query_silent(conn1, (char*) "SELECT * FROM t1;") == 0)
            {
                __sync_fetch_and_add(&data->i1, 1);
            }

            if (data->rwsplit_only == 0)
            {
                execute_query_silent(conn2, (char*) "SELECT * FROM t1;");
                execute_query_silent(conn3, (char*) "SELECT * FROM t1;");
            }
        }
    }

    if (conn1)
    {
        mysql_close(conn1);
    }
    if (conn2)
    {
        mysql_close(conn2);
    }
    if (conn3)
    {
        mysql_close(conn3);
    }
    return NULL;
}

void* query_thread2(void* ptr)
{
    MYSQL* conn1;
    MYSQL* conn2;
    MYSQL* conn3;
    thread_data* data = (thread_data*) ptr;
    auto mxs_ip = data->Test->maxscale->ip4();
    auto mxs_user = data->Test->maxscale->user_name();
    auto mxs_pw = data->Test->maxscale->password();

    // conn1 = data->Test->maxscales->open_rwsplit_connection(0);
    conn1 = open_conn_db_timeout(data->Test->maxscale->rwsplit_port, mxs_ip,
                                 "test",
                                 mxs_user,
                                 mxs_pw,
                                 20,
                                 data->Test->maxscale_ssl);
    if (data->rwsplit_only == 0)
    {
        // conn2 = data->Test->maxscales->open_readconn_master_connection(0);
        // conn3 = data->Test->maxscales->open_readconn_slave_connection(0);

        conn2 = open_conn_db_timeout(data->Test->maxscale->readconn_master_port, mxs_ip,
                                     "test",
                                     mxs_user,
                                     mxs_pw,
                                     20,
                                     data->Test->maxscale_ssl);
        // if (mysql_errno(conn2) != 0) { conn_err++; }
        conn3 = open_conn_db_timeout(data->Test->maxscale->readconn_slave_port, mxs_ip,
                                     "test",
                                     mxs_user,
                                     mxs_pw,
                                     20,
                                     data->Test->maxscale_ssl);
        // if (mysql_errno(conn3) != 0) { conn_err++; }
    }
    while (data->exit_flag == 0)
    {
        sleep(1);
        if (execute_query_silent(conn1, (char*) "SELECT * FROM t1;") == 0)
        {
            __sync_fetch_and_add(&data->i2, 1);
        }
        if (data->rwsplit_only == 0)
        {
            execute_query_silent(conn2, (char*) "SELECT * FROM t1;");
            execute_query_silent(conn3, (char*) "SELECT * FROM t1;");
        }
    }
    mysql_close(conn1);
    if (data->rwsplit_only == 0)
    {
        mysql_close(conn2);
        mysql_close(conn3);
    }
    return NULL;
}
