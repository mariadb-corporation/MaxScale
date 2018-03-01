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
    sleep(15);

    Test->set_timeout(30);
    Test->tprintf("Connecting to all MaxScale services, expecting error\n");
    Test->connect_maxscale();

    Test->set_timeout(30);
    Test->tprintf("Trying some queries, expecting failure, but not a crash\n");
    execute_query(Test->conn_rwsplit, "DROP TABLE IF EXISTS t1");
    execute_query(Test->conn_rwsplit, "CREATE TABLE t1 (x INT)");
    execute_query(Test->conn_rwsplit, "INSERT INTO t1 (x) VALUES (1)");
    execute_query(Test->conn_rwsplit, "select * from t1");
    execute_query(Test->conn_master, "select * from t1");
    execute_query(Test->conn_slave, "select * from t1");

    Test->set_timeout(10);
    Test->close_maxscale_connections();

    Test->set_timeout(30);
    Test->repl->unblock_all_nodes();

    Test->stop_timeout();
    sleep(15);
    Test->check_log_err("fatal signal 11", false);
    Test->check_log_err("Failed to create new router session for service", true);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

