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

    printf("Connecting to all nodes\n"); fflush(stdout);
    Test->repl->connect();
    for (int i = 0; i < Test->repl->N; i++) {
        printf("set max_connections = 20 for node %d\n", i); fflush(stdout);
        execute_query(Test->repl->nodes[i], (char *) "set global max_connections = 20;");
    }
    Test->repl->close_connections();

    printf("Start load\n"); fflush(stdout);
    load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 100, Test, &i1, &i2, 0, false);
    printf("Sleeping\n"); fflush(stdout);
    sleep(10);
    //load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 1000, Test, &i1, &i2, 0, false);
    //sleep(10);
    printf("Start load again\n"); fflush(stdout);
    load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 100, Test, &i1, &i2, 0, false);

    printf("restoring nodes\n"); fflush(stdout);
    Test->repl->connect();
    for (int i = 0; i < Test->repl->N; i++) {
        printf("Trying to flusg node %d\n", i); fflush(stdout);
        if (execute_query(Test->repl->nodes[i], (char *) "flush hosts;") !=0 ) {
            printf("node %i flusgh failed\n", i); fflush(stdout);
        }
        printf("Trying to set max_connections for node %d\n", i); fflush(stdout);
        if (execute_query(Test->repl->nodes[i], (char *) "set global max_connections = 151;") != 0) {
            printf("set max_connections failed for node %d\n", i); fflush(stdout);
        }
    }
    Test->repl->close_connections();

    check_log_err((char *) "refresh rate limit exceeded", FALSE);

    printf("Sleeping\n"); fflush(stdout);
    sleep(40);

    global_result += check_maxscale_alive();
    Test->repl->start_replication();
    Test->copy_all_logs(); return(global_result);
}
