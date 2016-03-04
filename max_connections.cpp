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

    for (int i = 0; i < CONNECTIONS - 1; i++)
    {
        Test->set_timeout(30);
        mysql[i] = Test->open_rwsplit_connection();
        Test->add_result(execute_query(mysql[i], "select 1"), "Query should succeed");
    }

    Test->set_timeout(30);
    mysql[CONNECTIONS - 1] = Test->open_rwsplit_connection();
    Test->add_result(!execute_query(mysql[CONNECTIONS - 1], "select 1"), "Query should not succeed");

    Test->stop_timeout();
    Test->check_maxscale_alive();
    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 100;");
    Test->copy_all_logs(); return(Test->global_result);

}
