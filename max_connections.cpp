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
        Test->set_timeout(30);
        mysql[i] = Test->open_rwsplit_connection();
        if(execute_query(mysql[i], "select 1"))
        {
            /** Monitors and such take up some connections so we'll set the
             * limit to the point where we know it'll start failing.*/
            limit = i;
            mysql_close(mysql[limit]);
            mysql_close(mysql[limit - 1]);
        }
    }

    sleep(5);
    Test->tprintf("Found limit, %d connections", limit);
    for (int i = 0; i < 100; i++)
    {
        Test->set_timeout(30);
        mysql[limit - 1] = Test->open_rwsplit_connection();
        mysql[limit] = Test->open_rwsplit_connection();
        Test->add_result(execute_query(mysql[limit - 1], "select 1"), "Query should not succeed\n");
        Test->add_result(!execute_query(mysql[limit], "select 1"), "Query should not succeed\n");
        mysql_close(mysql[limit - 1]);
        mysql_close(mysql[limit]);
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
