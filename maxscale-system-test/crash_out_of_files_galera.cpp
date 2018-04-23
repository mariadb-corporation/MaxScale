/**
 * @file crash_ot_of_files_galera.cpp Tries to open to many connections, expect no crash, Galera backend
 * - set global max_connections = 20
 * - create load on RWSplit using big number of threads (e.g. 100)
 * - check that no backends are disconnected with error ""refresh rate limit exceeded"
 */

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
    for (int i = 0; i < Test->galera->N; i++)
    {
        execute_query(Test->galera->nodes[i], (char *) "flush hosts;");
        execute_query(Test->galera->nodes[i], (char *) "set global max_connections = 151;");
    }
    Test->galera->close_connections();

    Test->check_log_err(0, (char *) "refresh rate limit exceeded", false);

    Test->galera->execute_query_all_nodes((char *) "set global max_connections = 100;");

    Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}

