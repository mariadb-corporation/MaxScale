/**
 * @file mxs657_restart.cpp Regression case for MXS-657 ("Debug assertion when service is shut down and restarted repeatedly")
 * - playing with 'restart service' and restart Maxscale under load
 */


#include "testconnections.h"
#include "sql_t1.h"
#include "get_com_select_insert.h"

#include "big_load.h"


TestConnections * Test ;

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int exit_flag = 0;
int start_flag = 0;
int restart_flag = 0; // 0 - maxadmin restart service, 1 - restart maxscale
unsigned int old_slave;
void *kill_vm_thread( void *ptr );

int main(int argc, char *argv[])
{
    Test = new TestConnections(argc, argv);
    pthread_t restart_t;
    int i, j;


    Test->tprintf("Connecting to RWSplit %s\n", Test->maxscales->IP[0]);

    Test->set_timeout(2000);

    pthread_create(&restart_t, NULL, kill_vm_thread, NULL);

    int iter = 1000;
    if (Test->smoke)
    {
        iter = 100;
    }

    for (i = 0; i < iter; i++)
    {
        Test->tprintf("i= %d\n", i);
        Test->maxscales->connect_maxscale(0);
        for (j = 0; j < iter; j++)
        {
            execute_query_silent(Test->maxscales->conn_rwsplit[0], "SELECT 1");

        }
        Test->maxscales->close_maxscale_connections(0);
        if (i > iter)
        {
            restart_flag = 1;
        }
    }

    restart_flag = 0;

    long int selects[256];
    long int inserts[256];
    long int new_selects[256];
    long int new_inserts[256];
    long int i1, i2;

    int threads_num = 25;
    if (Test->smoke)
    {
        threads_num = 15;
    }
    Test->tprintf("Increasing connection and error limits on backend nodes.\n");
    Test->repl->connect();
    for ( i = 0; i < Test->repl->N; i++)
    {
        execute_query(Test->repl->nodes[i], (char *) "set global max_connections = 300;");
        execute_query(Test->repl->nodes[i], (char *) "set global max_connect_errors = 100000;");
    }
    Test->repl->close_connections();

    Test->tprintf("Creating query load with %d threads and use maxadmin service restart...\n", threads_num);
    Test->set_timeout(1200);
    load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], threads_num, Test, &i1, &i2, 1, false,
         false);
    restart_flag = 1;
    Test->set_timeout(1200);
    Test->tprintf("Creating query load with %d threads and restart maxscalen", threads_num);
    load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], threads_num, Test, &i1, &i2, 1, false,
         false);

    Test->tprintf("Exiting ...\n");
    exit_flag = 1;
    pthread_join(restart_t, NULL);

    Test->tprintf("Checxking if MaxScale is still alive!\n");
    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}


void *kill_vm_thread( void *ptr )
{
    while (exit_flag == 0)
    {
        sleep(2);
        if (restart_flag == 0)
        {
            Test->maxscales->execute_maxadmin_command(0, (char *  ) "restart service \"RW Split Router\"");
        }
        else
        {
            Test->maxscales->restart_maxscale(0);
        }
    }

    return NULL;
}

