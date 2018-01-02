/**
 * Test for MXS-1323.
 * - Check that retried reads work with persistent connections
 */

#include "testconnections.h"
#include <sstream>

static bool running = true;

void* async_query(void* data)
{
    TestConnections *test = (TestConnections*)data;

    while (running && test->global_result == 0)
    {
        MYSQL* conn = test->maxscales->open_rwsplit_connection(0);

        for (int i = 0; i < 50 && running && test->global_result == 0; i++)
        {
            const char* query = "SET @a = (SELECT SLEEP(1))";
            test->try_query(conn, query);
        }

        mysql_close(conn);
    }

    return NULL;
}

#define NUM_THR 5

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);
    pthread_t query_thr[NUM_THR];
    std::stringstream ss;

    ss << "CREATE OR REPLACE TABLE test.t1 (id INT)";
    test.maxscales->connect_maxscale(0);
    test.try_query(test.maxscales->conn_rwsplit[0], ss.str().c_str());

    ss.str("");
    ss << "INSERT INTO test.t1 VALUES (0)";
    for (int i = 1; i <= 10000; i++)
    {
        ss << ",(" << i << ")";
    }
    test.try_query(test.maxscales->conn_rwsplit[0], ss.str().c_str());

    test.maxscales->close_maxscale_connections(0);

    if (test.global_result)
    {
        return test.global_result;
    }

    for (int i = 0; i < NUM_THR; i++)
    {
        pthread_create(&query_thr[i], NULL, async_query, &test);
    }

    for (int i = 0; i < 3 && test.global_result == 0; i++)
    {
        test.tprintf("Round %d", i + 1);
        test.repl->block_node(1);
        sleep(5);
        test.repl->unblock_node(1);
        sleep(5);
    }

    running = false;

    for (int i = 0; i < NUM_THR; i++)
    {
        test.set_timeout(10);
        pthread_join(query_thr[i], NULL);
    }

    return test.global_result;
}
