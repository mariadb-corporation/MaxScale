/**
 * Repeatedly connect to maxscale while the backends reject all connections
 *
 * MaxScale should not crash
 */
#include <my_config.h>
#include "testconnections.h"

int main(int argc, char** argv)
{
    MYSQL *mysql[1000];
    TestConnections * Test = new TestConnections(argc, argv);
    Test->stop_timeout();
    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 10;");
    sleep(5);
    int limit = 0;

    for (int i = 0; i < 20; i++)
    {
        Test->tprintf("Opening connection %d\n", i + 1);
        Test->set_timeout(30);
        mysql[i] = Test->open_readconn_master_connection();
        if(execute_query_silent(mysql[i], "select 1"))
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
    }

    for (int x = 0; x < 10; x++)
    {
        Test->tprintf("Creating 100 connections...\n");
        for (int i = limit; i < 100 + limit; i++)
        {
            Test->set_timeout(30);
            mysql[i] = Test->open_readconn_master_connection();
            execute_query_silent(mysql[i], "select 1");
        }
        Test->stop_timeout();
        sleep(10);
    }

    for (int i = 0; i < limit - 1; i++)
    {
        Test->set_timeout(30);
        mysql_close(mysql[i]);
    }

    sleep(5);
    Test->stop_timeout();
    Test->check_maxscale_alive();
    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 100;");
    Test->copy_all_logs(); return(Test->global_result);

}
