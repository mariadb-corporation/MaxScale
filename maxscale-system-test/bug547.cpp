/**
 * @file bug547.cpp regression case for bug 547 and bug 594 ( "get_dcb fails if slaves are not available" and "Maxscale fails to start without anything in the logs if there is no slave available" )
 * Behaviour has been changed and this test check only for crash
 * - block all slaves
 * - try some queries (create table, do INSERT using RWSplit router)
 * - check there is no crash
 */

/*
Vilho Raatikka 2014-09-16 07:43:54 UTC
get_dcb function returns the backend descriptor for router. Some merge has broken the logic and in case of non-existent slave the router simply fails to find a backend server although master would be available.
Comment 1 Vilho Raatikka 2014-09-16 09:40:14 UTC
get_dcb now searches master if slaves are not available.
*/

// also relates to bug594
// all slaves in MaxScale config have wrong IP


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int i;

    for (i = 1; i < Test->repl->N; i++)
    {
        Test->set_timeout(20);
        Test->repl->block_node(i);
    }

    Test->set_timeout(30);
    sleep(5);

    Test->set_timeout(30);
    Test->tprintf("Connecting to all MaxScale services, expecting error\n");
    Test->add_result(Test->maxscales->connect_maxscale(0) == 0, "Connection should fail");

    Test->set_timeout(30);
    Test->tprintf("Trying some queries, expecting failure, but not a crash\n");
    execute_query(Test->maxscales->conn_rwsplit[0], "DROP TABLE IF EXISTS t1");
    execute_query(Test->maxscales->conn_rwsplit[0], "CREATE TABLE t1 (x INT)");
    execute_query(Test->maxscales->conn_rwsplit[0], "INSERT INTO t1 (x) VALUES (1)");
    execute_query(Test->maxscales->conn_rwsplit[0], "select * from t1");
    execute_query(Test->maxscales->conn_master[0], "select * from t1");
    execute_query(Test->maxscales->conn_slave[0], "select * from t1");

    Test->set_timeout(10);
    Test->maxscales->close_maxscale_connections(0);

    Test->set_timeout(30);
    Test->repl->unblock_all_nodes();

    Test->stop_timeout();
    Test->check_log_err(0, "fatal signal 11", false);
    Test->check_log_err(0, "Failed to create new router session for service", true);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

