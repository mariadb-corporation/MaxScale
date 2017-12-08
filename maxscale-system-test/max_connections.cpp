/**
 * @file max_connections.cpp Creates a number of connections > max_connections setting
 * - set global max_connections = 20
 * - create 20 connections, find on which iteration query start to fail
 * - when limit is found close last 2 connections
 * - in the loop: open two connections, expect first to succeed, second to fail, close them both and repeat
 * - close all connections
 */

#include "testconnections.h"

#define CONNECTIONS 21
#define ITER 25

int main(int argc, char** argv)
{
    MYSQL *mysql[CONNECTIONS];
    TestConnections * Test = new TestConnections(argc, argv);
    Test->stop_timeout();
    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 20;");
    sleep(5);
    int limit = 0;

    for (int i = 0; i < CONNECTIONS - 1; i++)
    {
        Test->tprintf("Opening connection %d\n", i + 1);
        Test->set_timeout(30);
        mysql[i] = Test->maxscales->open_rwsplit_connection(0);
        if (execute_query_silent(mysql[i], "select 1"))
        {
            /** Monitors and such take up some connections so we'll set the
             * limit to the point where we know it'll start failing.*/
            Test->stop_timeout();
            limit = i;
            mysql_close(mysql[limit]);
            mysql_close(mysql[limit - 1]);
            Test->tprintf("Found limit, %d connections\n", limit);
            break;
        }
        Test->stop_timeout();
        sleep(1);
    }

    sleep(5);
    Test->tprintf("Opening two connections for %d times. One should succeed while the other should fail.\n",
                  ITER);
    for (int i = 0; i < ITER; i++)
    {
        Test->set_timeout(30);
        mysql[limit - 1] = Test->maxscales->open_rwsplit_connection(0);
        mysql[limit] = Test->maxscales->open_rwsplit_connection(0);
        Test->add_result(execute_query_silent(mysql[limit - 1], "select 1"), "Query should succeed\n");
        Test->add_result(!execute_query_silent(mysql[limit], "select 1"), "Query should fail\n");
        mysql_close(mysql[limit - 1]);
        mysql_close(mysql[limit]);
        sleep(2);
    }

    Test->set_timeout(30);
    for (int i = 0; i < limit - 1; i++)
    {
        mysql_close(mysql[i]);
    }

    sleep(5);
    Test->stop_timeout();
    Test->check_maxscale_alive(0);
    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 100;");
    int rval = Test->global_result;
    delete Test;
    return rval;

}
