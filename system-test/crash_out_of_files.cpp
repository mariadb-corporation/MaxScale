/**
 * @file crash_ot_of_files.cpp Tries to open to many connections, expect no crash
 * - set global max_connections = 20
 * - create load on RWSplit using big number of threads (e.g. 100)
 * - check that no backends are disconnected with error ""refresh rate limit exceeded"
 */

#include <maxtest/big_load.hh>
#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);

    Test->reset_timeout();
    Test->repl->execute_query_all_nodes((char*) "set global max_connections = 20;");

    long int i1, i2;
    long int selects[256];
    long int inserts[256];
    long int new_selects[256];
    long int new_inserts[256];

    Test->tprintf("Start load\n");
    Test->reset_timeout();
    load(&new_inserts[0], &new_selects[0], &selects[0], &inserts[0], 100, Test, &i1, &i2, 0, false, false);

    Test->tprintf("restoring nodes\n");
    Test->reset_timeout();
    Test->repl->connect();

    for (int i = 0; i < Test->repl->N; i++)
    {
        Test->tprintf("Trying to flush node %d\n", i);
        Test->add_result(execute_query(Test->repl->nodes[i], (char*) "flush hosts;"),
                         "node %i flush failed\n",
                         i);
        Test->tprintf("Trying to set max_connections for node %d\n", i);
        Test->add_result(execute_query(Test->repl->nodes[i], (char*) "set global max_connections = 151;"),
                         "set max_connections failed for node %d\n",
                         i);
    }

    Test->repl->close_connections();

    Test->log_excludes("Refresh rate limit exceeded");
    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;
    return rval;
}
