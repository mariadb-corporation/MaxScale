#include <my_config.h>
#include "testconnections.h"
#include "sql_t1.h"
#include "get_com_select_insert.h"

#include "big_load.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    int q;
    int i1, i2;

    int selects[256];
    int inserts[256];
    int new_selects[256];
    int new_inserts[256];

    Test->read_env();
    Test->print_env();

    Test->repl->connect();
    for (int i = 0; i < Test->galera->N; i++) {
        execute_query(Test->galera->nodes[i], (char *) "set global max_connections = 20;");
    }
    Test->repl->close_connections();


    load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 100, Test, &i1, &i2, 0, true);
    sleep(10);
    //load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 1000, Test, &i1, &i2, 0, true);
    //sleep(10);
    load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 100, Test, &i1, &i2, 0, true);

    Test->galera->connect();
    for (int i = 0; i < Test->galera->N; i++) {
        execute_query(Test->galera->nodes[i], (char *) "flush hosts;");
        execute_query(Test->galera->nodes[i], (char *) "set global max_connections = 151;");
    }
    Test->galera->close_connections();

    check_log_err((char *) "refresh rate limit exceeded", FALSE);

    global_result += check_maxscale_alive();
    Test->copy_all_logs(); return(global_result);
}

