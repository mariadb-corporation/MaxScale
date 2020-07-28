/**
 * @file mxs922_scaling.cpp MXS-922: Server scaling test
 *
 */

#include "testconnections.h"
#include "config_operations.h"

static bool running = true;

void* query_thread(void* data)
{
    TestConnections* test = static_cast<TestConnections*>(data);

    MYSQL* mysql = test->maxscales->open_rwsplit_connection(0);
    my_bool yes = true;
    mysql_options(mysql, MYSQL_OPT_RECONNECT, &yes);

    while (running)
    {
        execute_query_silent(mysql, "SELECT @@server_id");
        execute_query_silent(mysql, "SELECT last_insert_id()");
    }

    mysql_close(mysql);

    return NULL;
}


int main(int argc, char* argv[])
{
    TestConnections* test = new TestConnections(argc, argv);
    Config config(test);

    config.create_all_listeners();
    config.create_monitor("mysql-monitor", "mysqlmon", 500);

    int num_threads = 5;
    int iterations = test->smoke ? 5 : 25;
    pthread_t threads[num_threads];

    test->tprintf("Creating client threads");

    for (int i = 0; i < num_threads; i++)
    {
        pthread_create(&threads[i], NULL, query_thread, test);
    }


    test->tprintf("Adding and removing servers for %d seconds.", iterations * test->repl->N);

    for (int x = 0; x < iterations; x++)
    {
        for (int i = 0; i < test->repl->N; i++)
        {
            if ((x + i) % 2 == 0)
            {
                config.create_server(i);
                config.add_server(i);
            }
            else
            {
                config.remove_server(i);
                config.destroy_server(i);
            }

            sleep(1);
        }
    }

    running = false;

    for (int i = 0; i < num_threads; i++)
    {
        pthread_join(threads[i], NULL);
    }

    /** Make sure the servers exist before checking that connectivity is OK */
    for (int i = 0; i < test->repl->N; i++)
    {
        config.create_server(i);
        config.add_server(i);
    }

    sleep(1);

    test->check_maxscale_alive(0);

    int rval = test->global_result;
    delete test;
    return rval;
}
