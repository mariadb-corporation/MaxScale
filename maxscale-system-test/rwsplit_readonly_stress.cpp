/**
 * @file rwsplit_readonly.cpp Test of the read-only mode for readwritesplit when master fails with load
 * - start query threads which does SELECTs in the loop
 * - every 10 seconds block Master and then after another 10 seconds unblock master
 */


#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include "testconnections.h"
#include "maxadmin_operations.h"

#define THREADS 16

static int running = 0;

void* query_thread(void *data)
{
    TestConnections *Test = (TestConnections*)data;
    int iter = 0;

    while (!running)
    {
        sleep(1);
    }

    while (running && Test->global_result == 0)
    {
        MYSQL* mysql;
        const char* type;

        if (iter % 2 == 0)
        {
            mysql = Test->maxscales->open_readconn_slave_connection(0);
            type = "master_failure_mode=error_on_write";
        }
        else
        {
            mysql = Test->maxscales->open_readconn_master_connection(0);
            type = "master_failure_mode=fail_on_write";
        }

        if (!mysql)
        {
            Test->tprintf("Failed to connect to MaxScale.\n");
        }

        for (int i = 0; i < 100 && Test->global_result == 0; i++)
        {
            if (execute_query_silent(mysql, "select repeat('a', 1000)"))
            {
                Test->add_result(1, "Query number %d, iteration %d for '%s' failed: %s\n", i, iter, type, mysql_error(mysql));
            }
        }
        mysql_close(mysql);
        iter++;
    }

    return NULL;
}

int main(int argc, char *argv[])
{

    TestConnections *Test = new TestConnections(argc, argv);
    pthread_t threads[THREADS];

    for (int i = 0; i < THREADS; i++)
    {
        pthread_create(&threads[i], NULL, query_thread, Test);
    }

    running = 1;

    int iterations = (Test->smoke ? 5 : 25);

    for (int i = 0; i < iterations && Test->global_result == 0; i++)
    {

        Test->tprintf("Blocking master");
        Test->repl->block_node(0);

        if (Test->global_result == 0)
        {
            sleep(10);
        }

        Test->tprintf("Unblocking master");
        Test->repl->unblock_node(0);

        if (Test->global_result == 0)
        {
            sleep(10);
        }
    }

    Test->tprintf("Waiting for all threads to finish\n");
    running = 0;

    for (int i = 0; i < THREADS; i++)
    {
        void* val;
        pthread_join(threads[i], &val);
    }

    /** Clean up test environment */
    Test->repl->flush_hosts();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
