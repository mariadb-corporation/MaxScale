#include <my_config.h>
#include "testconnections.h"
#include "sql_t1.h"
#include "get_com_select_insert.h"

#include "big_load.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    long int i1, i2;

    long int selects[256];
    long int inserts[256];
    long int new_selects[256];
    long int new_inserts[256];

    Test->tprintf("Connecting to all nodes\n");
    Test->repl->connect();
    for (int i = 0; i < Test->repl->N; i++) {
        Test->tprintf("set max_connections = 20 for node %d\n", i);
        execute_query(Test->repl->nodes[i], (char *) "set global max_connections = 20;");
    }
    Test->repl->close_connections();

    Test->tprintf("Start load\n");
    Test->set_timeout(1200);
    load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 100, Test, &i1, &i2, 0, false, false);
    Test->tprintf("Sleeping\n");
    sleep(10);

    Test->tprintf("Start load again\n"); fflush(stdout);
    Test->set_timeout(1200);
    load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 100, Test, &i1, &i2, 0, false, false);

    Test->tprintf("restoring nodes\n");
    Test->set_timeout(60);
    Test->repl->connect();
    for (int i = 0; i < Test->repl->N; i++) {
        Test->tprintf("Trying to flusg node %d\n", i);
        Test->add_result(execute_query(Test->repl->nodes[i], (char *) "flush hosts;"), "node %i flusgh failed\n", i);
        Test->tprintf("Trying to set max_connections for node %d\n", i);
        Test->add_result(execute_query(Test->repl->nodes[i], (char *) "set global max_connections = 151;"), "set max_connections failed for node %d\n", i);
    }
    Test->repl->close_connections();

    Test->check_log_err((char *) "refresh rate limit exceeded", FALSE);

    Test->tprintf("Sleeping\n");
    Test->stop_timeout();
    sleep(40);

    Test->check_maxscale_alive();
    Test->set_timeout(600);
    Test->repl->start_replication();
    Test->copy_all_logs(); return(Test->global_result);
}
