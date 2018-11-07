/**
 * @file mxs812_2.cpp - Execute binary protocol prepared statements while master is blocked, checks "Current
 * no. of conns" after the test
 * - start threads which prepares and executes simple statement in the loop
 * - every 5 seconds block and after another 5 seconds unblock Master
 * - checks "Current no. of conns" after the test, expect 0
 */

#include "testconnections.h"

int test_ps(TestConnections* Test, MYSQL* conn)
{
    const char select_stmt[] = "SELECT ?, ?, ?, ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);

    mysql_stmt_prepare(stmt, select_stmt, sizeof(select_stmt) - 1);

    int value = 1;
    MYSQL_BIND param[4];

    param[0].buffer_type = MYSQL_TYPE_LONG;
    param[0].is_null = 0;
    param[0].buffer = &value;
    param[1].buffer_type = MYSQL_TYPE_LONG;
    param[1].is_null = 0;
    param[1].buffer = &value;
    param[2].buffer_type = MYSQL_TYPE_LONG;
    param[2].is_null = 0;
    param[2].buffer = &value;
    param[3].buffer_type = MYSQL_TYPE_LONG;
    param[3].is_null = 0;
    param[3].buffer = &value;

    mysql_stmt_bind_param(stmt, param);
    mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);

    return 0;
}

static bool running = true;

void* test_thr(void* data)
{
    TestConnections* Test = (TestConnections*)data;

    while (running)
    {
        MYSQL* mysql = Test->maxscales->open_rwsplit_connection(0);

        for (int i = 0; i < 3; i++)
        {
            test_ps(Test, mysql);
        }

        mysql_close(mysql);
    }

    return NULL;
}

#define THREADS 5

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    pthread_t thr[THREADS];
    int iter = 5;

    Test->tprintf("Starting %d query threads", THREADS);

    for (int i = 0; i < THREADS; i++)
    {
        pthread_create(&thr[i], NULL, test_thr, Test);
    }

    for (int i = 0; i < iter; i++)
    {
        Test->tprintf("Blocking master");
        Test->repl->block_node(0);
        Test->maxscales->wait_for_monitor();
        Test->tprintf("Unblocking master");
        Test->repl->unblock_node(0);
        Test->maxscales->wait_for_monitor();
    }

    running = false;

    Test->tprintf("Joining threads");
    for (int i = 0; i < THREADS; i++)
    {
        pthread_join(thr[i], NULL);
    }

    Test->stop_timeout();

    Test->check_maxscale_alive(0);
    Test->check_current_operations(0, 0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
