/**
 * @file server_lag.cpp  Create high INSERT load to create slave lag and check that Maxscale start routing queries to Master
 *
 * - in Maxscqale.cnf set max_slave_replication_lag=20
 * - in parallel thread execute as many INSERTs as possible
 * - using "select @@server_id;" check that queries go to one of the slave
 * - wait when slave lag > 20 (control lag using maxadmin interface)
 * - check that now queries go to Master
 */


#include "testconnections.h"
#include "sql_t1.h"
#include "maxadmin_operations.h"

char sql[1000000];

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int exit_flag = 0;
int exited = 0;
void *query_thread( void *ptr );
void *checks_thread( void *ptr);

TestConnections * Test;

int check_lag(int * min_lag)
{
    char result[1024];
    char server_id[1024];
    char ma_cmd[256];
    int res_d;
    int server1_id_d;
    int server_id_d;
    int i;
    int ret = 0;

    *min_lag = 0;
    for (i = 1; i < Test->repl->N; i++ )
    {
        sprintf(ma_cmd, "show server server%d", i + 1);
        get_maxadmin_param(Test->maxscales->IP[0], (char *) "admin", Test->maxscales->maxadmin_password[0], ma_cmd,
                           (char *) "Slave delay:", result);
        sscanf(result, "%d", &res_d);
        Test->tprintf("server%d lag: %d\n", i + 1, res_d);
        if (i == 1)
        {
            *min_lag = res_d;
        }
        if (*min_lag > res_d)
        {
            *min_lag = res_d;
        }
    }
    Test->tprintf("Minimum lag: %d\n", *min_lag);
    Test->connect_rwsplit();
    find_field(Test->maxscales->conn_rwsplit[0], (char *) "select @@server_id; -- maxscale max_slave_replication_lag=20",
               (char *) "@@server_id", &server_id[0]);
    Test->close_rwsplit();
    sscanf(server_id, "%d", &server_id_d);
    Test->tprintf("Connected to the server with server_id %d\n", server_id_d);
    if ((server1_id_d == server_id_d))
    {
        Test->add_result(1, "Connected to the master!\n");
        ret = 0;
    }
    else
    {
        Test->tprintf("Connected to slave\n");
        ret = 1;
    }
    return ret;
}

int main(int argc, char *argv[])
{

    char server1_id[1024];
    int server1_id_d;
    int i;
    int min_lag = 0;
    int ms;

    Test = new TestConnections(argc, argv);
    Test->set_timeout(2000);

    Test->repl->connect();
    Test->connect_rwsplit();

    // connect to the MaxScale server (rwsplit)

    if (Test->maxscales->conn_rwsplit[0] == NULL )
    {
        printf("Can't connect to MaxScale\n");
        int rval = Test->global_result;
        delete Test;
        exit(1);
    }
    else
    {
        for ( i = 0; i < Test->repl->N; i++)
        {
            Test->tprintf("set max_connections = 200 for node %d\n", i);
            execute_query(Test->repl->nodes[i], (char *) "set global max_connections = 200;");
        }

        create_t1(Test->maxscales->conn_rwsplit[0]);
        create_t2(Test->maxscales->conn_rwsplit[0]);

        create_insert_string(sql, 50000, 1);
        Test->tprintf("sql_len=%lu\n", strlen(sql));
        /*      for ( i = 0; i < 100; i++) {
                    Test->try_query(Test->maxscales->conn_rwsplit[0], sql);
                }*/

        pthread_t threads[1000];
        //pthread_t check_thread;
        int  iret[1000];
        //int check_iret;
        int j;
        exit_flag = 0;
        /* Create independent threads each of them will execute function */
        for (j = 0; j < 100; j++)
        {
            iret[j] = pthread_create( &threads[j], NULL, query_thread, &sql);
        }

        execute_query(Test->maxscales->conn_rwsplit[0], (char *) "select @@server_id; -- maxscale max_slave_replication_lag=10");

        find_field(Test->repl->nodes[0], (char *) "select @@server_id;", (char *) "@@server_id", &server1_id[0]);
        sscanf(server1_id, "%d", &server1_id_d);
        Test->tprintf("Master server_id: %d\n", server1_id_d);

        Test->close_rwsplit();

        for (i = 0; i < 1000; i++)
        {
            ms = check_lag(&min_lag);
            if ((ms = 0) && (min_lag < 20))
            {
                Test->add_result(1, "Lag is small, but connected to master\n");
            }
            if ((ms = 1) && (min_lag > 20))
            {
                Test->add_result(1, "Lag is big, but connected to slave\n");
            }
        }

        exit_flag = 1;
    }
    while (exited == 0)
    {
        Test->tprintf("Waiting for load thread end\n");
        sleep(5);
    }
    Test->repl->close_connections();
    Test->repl->start_replication();

    int rval = Test->global_result;
    delete Test;
    return rval;
}


void *query_thread( void *ptr )
{
    MYSQL * conn;
    conn = open_conn(Test->repl->port[0], Test->repl->IP[0], Test->repl->user_name, Test->repl->password,
                     Test->repl->ssl);
    while (exit_flag == 0)
    {
        //execute_query(conn, (char *) "INSERT INTO t2 (x1, fl) SELECT x1,fl FROM t1");
        execute_query_silent(conn, (char *) ptr);
    }
    exited = 1;
    return NULL;
}

void *checks_thread( void *ptr )
{
    char result[1024];
    for (int i = 0; i < 1000; i++)
    {
        get_maxadmin_param(Test->maxscales->IP[0], (char *) "admin", Test->maxscales->maxadmin_password[0],
                           (char *) "show server server2", (char *) "Slave delay:", result);
        printf("server2: %s\n", result);
        get_maxadmin_param(Test->maxscales->IP[0], (char *) "admin", Test->maxscales->maxadmin_password[0],
                           (char *) "show server server3", (char *) "Slave delay:", result);
        printf("server3: %s\n", result);
        get_maxadmin_param(Test->maxscales->IP[0], (char *) "admin", Test->maxscales->maxadmin_password[0],
                           (char *) "show server server4", (char *) "Slave delay:", result);
        printf("server4: %s\n", result);
    }
    exit_flag = 1;
    return NULL;
}

