/**
 * @file bug547.cpp regression case for bug 547 and bug 594 ( "get_dcb fails if slaves are not available" and "Maxscale fails to start without anything in the logs if there is no slave available" )
 * Behaviour has been changed and this test check only for crash
 * - block all slaves
 * - try some queries (create table, do INSERT using RWSplit router)
 * - check there is no crash
 */

// also relates to bug594
// all slaves in MaxScale config have wrong IP

#include <my_config.h>
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
    Test->set_timeout(10);
    sleep(5);
    Test->set_timeout(10);
    Test->tprintf("Connecting to all MaxScale services, expecting error\n");
    Test->connect_maxscale();

    Test->set_timeout(30);
    Test->tprintf("Trying some queries, expecting failure, but not a crash\n");
    execute_query(Test->conn_rwsplit, (char *) "DROP TABLE IF EXISTS t1");
    execute_query(Test->conn_rwsplit, (char *) "CREATE TABLE t1 (x INT)");
    execute_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 (x) VALUES (1)");
    execute_query(Test->conn_rwsplit, (char *) "select * from t1");
    execute_query(Test->conn_master, (char *) "select * from t1");
    execute_query(Test->conn_slave, (char *) "select * from t1");

    Test->set_timeout(10);
    Test->close_maxscale_connections();

    Test->set_timeout(30);
    Test->repl->unblock_all_nodes();

    Test->stop_timeout();
    sleep(15);
    Test->check_log_err((char *) "fatal signal 11", false);
    Test->check_log_err((char *) "Failed to create Read Connection Router Master session", true);
    Test->check_log_err((char *) "Failed to create RW Split Router session", true);
    Test->check_log_err((char *) "Failed to create Read Connection Router Slave session", true);

    Test->copy_all_logs(); return(Test->global_result);
}

