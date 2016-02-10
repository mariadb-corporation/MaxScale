/**
 * @file bug547.cpp regression case for bug 547 and bug 594 ( "get_dcb fails if slaves are not available" and "Maxscale fails to start without anything in the logs if there is no slave available" )
 *
 * - block all slaves
 * - create table, do INSERT using RWSplit router
 * - do SELECT using all services
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
        Test->set_timeout(10);
        Test->repl->block_node(i);
    }
    Test->set_timeout(10);
    sleep(5);
    Test->set_timeout(10);
    Test->tprintf("Connecting to all MaxScale services\n");
    Test->add_result(Test->connect_maxscale(), "Error connection to Maxscale\n");

    Test->set_timeout(10);
    Test->tprintf("Creating table t1\n");
    Test->try_query(Test->conn_rwsplit, (char *) "DROP TABLE IF EXISTS t1;CREATE TABLE t1 (x INT); INSERT INTO t1 (x) VALUES (1)");

    Test->set_timeout(10);
    Test->tprintf("Select using RWSplit\n");
    Test->try_query(Test->conn_rwsplit, (char *) "select * from t1");

    Test->set_timeout(10);
    Test->tprintf("Select using ReadConn master\n");
    Test->try_query(Test->conn_master, (char *) "select * from t1");

    Test->set_timeout(10);
    Test->tprintf("Select using ReadConn slave\n");
    Test->try_query(Test->conn_slave, (char *) "select * from t1");

    Test->set_timeout(10);
    Test->close_maxscale_connections();

    Test->repl->unblock_all_nodes();

    Test->copy_all_logs(); return(Test->global_result);
}
