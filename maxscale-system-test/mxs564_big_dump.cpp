/**
 * @file mxs564_big_dump.cpp MXS-564 regression case ("Loading database dump through readwritesplit fails")
 * - configure Maxscale to use Galera cluster
 * - start several threads which are executing session command and then sending INSERT queries agaist RWSplit router
 * - after a while block first slave
 * - after a while block second slave
 * - check that all INSERTs are ok
 * - repeat with both RWSplit and ReadConn master maxscales->routers[0]
 * - check Maxscale is alive
 */


#include "testconnections.h"
#include "sql_t1.h"
//#include "get_com_select_insert.h"

typedef struct
{
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

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->stop_timeout();

    int threads_num = 4;
    openclose_thread_data data[threads_num];

    int i;
    int run_time = 100;

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
    }


    pthread_t thread1[threads_num];

    //Test->repl->flush_hosts();
    Test->set_timeout(20);
    int master = Test->maxscales->find_master_maxadmin(Test->galera);
    Test->stop_timeout();
    Test->tprintf(("Master is %d\n"), master);
    int k = 0;
    int x = 0;
    int slaves[2];
    while (k < 2 )
    {
        if (x != master)
        {
            slaves[k] = x;
            k++;
            x++;
        }
        else
        {
            x++;
        }
    }
    Test->tprintf(("Slave1 is %d\n"), slaves[0]);
    Test->tprintf(("Slave2 is %d\n"), slaves[1]);

    Test->set_timeout(20);
    Test->repl->connect();
    Test->maxscales->connect_maxscale(0);
    Test->set_timeout(20);
    create_t1(Test->maxscales->conn_rwsplit[0]);
    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 2000;");

    Test->set_timeout(20);
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "DROP TABLE IF EXISTS t1");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "CREATE TABLE t1 (x1 int, fl int)");

    for (i = 0; i < threads_num; i++)
    {
        data[i].rwsplit_only = 1;
    }
    /* Create independent threads each of them will execute function */
    for (i = 0; i < threads_num; i++)
    {
        pthread_create(&thread1[i], NULL, query_thread1, &data[i]);
    }
    Test->tprintf("Threads are running %d seconds \n", run_time);

    Test->set_timeout(3 * run_time + 60);
    sleep(20);
    sleep(run_time);
    Test->tprintf("Blocking slave %d\n", slaves[0]);
    Test->galera->block_node(slaves[0]);
    sleep(run_time);
    Test->galera->block_node(slaves[1]);
    Test->tprintf("Blocking slave %d\n", slaves[1]);
    sleep(run_time);
    Test->tprintf("Unblocking slaves\n");
    Test->galera->unblock_node(slaves[0]);
    Test->galera->unblock_node(slaves[1]);

    Test->set_timeout(120);
    Test->tprintf("Waiting for all threads exit\n");
    for (i = 0; i < threads_num; i++)
    {
        data[i].exit_flag = 1;
        pthread_join(thread1[i], NULL);
        Test->tprintf("exit %d\n", i);
    }

    Test->tprintf("all maxscales->routers[0] are involved, threads are running %d seconds more\n", run_time);

    for (i = 0; i < threads_num; i++)
    {
        data[i].rwsplit_only = 0;
    }
    for (i = 0; i < threads_num; i++)
    {
        pthread_create(&thread1[i], NULL, query_thread1, &data[i]);
    }

    Test->set_timeout(3 * run_time + 60);
    sleep(20);
    sleep(run_time);
    Test->tprintf("Blocking node %d\n", slaves[0]);
    Test->galera->block_node(slaves[0]);
    sleep(run_time);
    Test->tprintf("Blocking node %d\n", slaves[1]);
    Test->galera->block_node(slaves[1]);
    sleep(run_time);
    Test->tprintf("Unblocking nodes\n");
    Test->galera->unblock_node(slaves[0]);
    Test->galera->unblock_node(slaves[1]);

    Test->set_timeout(120);
    Test->tprintf("Waiting for all threads exit\n");
    for (i = 0; i < threads_num; i++)
    {
        data[i].exit_flag = 1;
        pthread_join(thread1[i], NULL);
    }

    sleep(5);

    Test->set_timeout(60);
    Test->tprintf("set global max_connections = 100 for all backends\n");
    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 100;");
    Test->tprintf("Drop t1\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "DROP TABLE IF EXISTS t1;");
    Test->maxscales->close_maxscale_connections(0);

    Test->tprintf("Checking if Maxscale alive\n");
    Test->check_maxscale_alive(0);
    //Test->tprintf("Checking log for unwanted errors\n");
    //Test->check_log_err(0, (char *) "due to authentication failure", false);
    //Test->check_log_err(0, (char *) "fatal signal 11", false);
    //Test->check_log_err(0, (char *) "due to handshake failure", false);
    //Test->check_log_err(0, (char *) "Refresh rate limit exceeded for load of users' table", false);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

void *query_thread1( void *ptr )
{
    openclose_thread_data * data = (openclose_thread_data *) ptr;
    char sql[1000000];
    sleep(data->thread_id);
    create_insert_string(sql, 1000, 2);

    data->conn1 = data->Test->maxscales->open_rwsplit_connection(0);
    if ((data->conn1 == NULL) || (mysql_errno(data->conn1) != 0 ))
    {
        data->Test->add_result(1, "Error connecting to RWSplit\n");
        return NULL;
    }

    data->Test->try_query(data->conn1, (char *) "SET SESSION SQL_LOG_BIN=0;");

    if (data->rwsplit_only == 0)
    {
        data->conn2 = data->Test->maxscales->open_readconn_master_connection(0);
        if ((data->conn2 == NULL) || (mysql_errno(data->conn2) != 0 ))
        {
            data->Test->add_result(1, "Error connecting to ReadConn Master\n");
            return NULL;
        }
        data->Test->try_query(data->conn2, (char *) "SET SESSION SQL_LOG_BIN=0;");
    }

    while (data->exit_flag == 0)
    {
        if (data->Test->try_query(data->conn1, sql))
        {
            data->Test->add_result(1, "Query to ReadConn Master failed\n");
            return NULL;
        }
        if (data->rwsplit_only == 0)
        {
            if (data->Test->try_query(data->conn2, sql))
            {
                data->Test->add_result(1, "Query to RWSplit failed\n");
                return NULL;
            }
        }
        data->i++;
    }
    if (data->conn1 != NULL)
    {
        mysql_close(data->conn1);
    }
    if (data->rwsplit_only == 0)
    {
        if (data->conn2 != NULL)
        {
            mysql_close(data->conn2);
        }
    }
    return NULL;
}
