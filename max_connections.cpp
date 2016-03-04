#include <my_config.h>
#include "testconnections.h"

#define CONNECTIONS 21

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
        mysql[i] = Test->open_rwsplit_connection();
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

    sleep(5);
    Test->tprintf("Opening two connection. One should succeed while the other should fail. \n");
    for (int i = 0; i < 50; i++)
    {
        Test->set_timeout(30);
        mysql[limit - 1] = Test->open_rwsplit_connection();
        mysql[limit] = Test->open_rwsplit_connection();
        Test->add_result(execute_query_silent(mysql[limit - 1], "select 1"), "Query should succeed");
        Test->add_result(!execute_query_silent(mysql[limit], "select 1"), "Query should fail");
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
    Test->check_maxscale_alive();
    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 100;");
    Test->copy_all_logs(); return(Test->global_result);

}
