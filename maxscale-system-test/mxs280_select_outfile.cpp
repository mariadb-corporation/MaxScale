/**
 * @file mxs280_select_outfile.cpp bug mxs280 regression case ("SELECT INTO OUTFILE query succeeds even if backed fails")
 *
 * - Create /tmp/t1.csv on all backends
 * - creat t1 table, put some data into it
 * - try SELECT * INTO OUTFILE '/tmp/t1.csv' FROM t1 and expect failure
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main(int argc, char *argv[])
{
    int i;
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    Test->connect_maxscale();

    Test->tprintf("Create /tmp/t1.csv on all backend nodes\n");
    for (i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(30);
        Test->repl->ssh_node(i, (char *) "touch /tmp/t1.csv", true);
    }

    Test->add_result(create_t1(Test->maxscales->conn_rwsplit[0]), "Error creating t1\n");
    Test->try_query(Test->maxscales->conn_rwsplit[0], (char *) "INSERT INTO t1 (x1, fl) VALUES (0, 0), (1, 0)");

    if ( (execute_query(Test->maxscales->conn_rwsplit[0], (char *) "SELECT * INTO OUTFILE '/tmp/t1.csv' FROM t1;")) == 0 )
    {
        Test->add_result(1, "SELECT INTO OUTFILE epected to fail, but it is OK\n");
    }



    Test->tprintf("Remove /tmp/t1.csv from all backend nodes\n");
    for (i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(30);
        Test->repl->ssh_node(i, (char *) "rm -rf /tmp/t1.csv", true);
    }

    Test->set_timeout(30);
    sleep(5);
    Test->check_log_err((char *) "Failed to execute session command in", true);
    Test->check_log_err((char *) "File '/tmp/t1.csv' already exists", true);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
