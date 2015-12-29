#include <my_config.h>
#include "testconnections.h"
#include "sql_t1.h"
#include "get_com_select_insert.h"

#include "big_load.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);
    int q;
    long int i1, i2;

    long int selects[256];
    long int inserts[256];
    long int new_selects[256];
    long int new_inserts[256];

    Test->read_env();
    Test->print_env();

    Test->galera->execute_query_all_nodes((char *) "set global max_connections = 20;");

    Test->set_timeout(1200);
    load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 100, Test, &i1, &i2, 0, true, false);
    sleep(10);
    //load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 1000, Test, &i1, &i2, 0, true, false);
    //sleep(10);
    Test->set_timeout(1200);
    load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 100, Test, &i1, &i2, 0, true, false);

    Test->set_timeout(20);
    Test->galera->connect();
    for (int i = 0; i < Test->galera->N; i++) {
        execute_query(Test->galera->nodes[i], (char *) "flush hosts;");
        execute_query(Test->galera->nodes[i], (char *) "set global max_connections = 151;");
    }
    Test->galera->close_connections();

    Test->check_log_err((char *) "refresh rate limit exceeded", FALSE);

    Test->galera->execute_query_all_nodes((char *) "set global max_connections = 100;");

    Test->check_maxscale_alive();
    Test->copy_all_logs(); return(Test->global_result);
}

