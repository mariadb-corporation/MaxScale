/**
 * @file mxs827_write_timeout "ReadWriteSplit only keeps used connection alive, query crashes after unused connection times out"
 * - SET wait_timeout=20
 * - do only SELECT during 30 seconds
 * - try INSERT
 */

#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    Test->connect_maxscale();

    Test->try_query(Test->maxscales->conn_rwsplit[0], "SET wait_timeout=20");

    create_t1(Test->maxscales->conn_rwsplit[0]);

    for (int i = 0; i < 30; i++)
    {
        Test->tprintf("Trying query %d\n", i);
        Test->set_timeout(10);
        Test->try_query(Test->maxscales->conn_rwsplit[0], "SELECT 1");
        sleep(1);
    }

    Test->try_query(Test->maxscales->conn_rwsplit[0], "INSERT INTO t1 VALUES (1, 1)");

    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}

