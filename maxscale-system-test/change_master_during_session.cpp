/**
 * @file change_master_during_seesion.cpp Tries to reconfigure replication setup to use another node as a Master
 * - connect to RWSplit
 * - reconfugure backend
 * - checks that after time > monitor_interval everything is ok
 */

#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;


int main(int argc, char *argv[])
{
    char sql[1024];
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(30);

    Test->repl->connect();

    printf("Connecting to RWsplit\n");
    Test->connect_rwsplit();
    Test->set_timeout(30);
    Test->add_result(create_t1(Test->conn_rwsplit), "Error creating 't1'\n");

    Test->try_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 (x1, fl) VALUES(0, 1);");
    Test->tprintf("Changing master to node 1\n");
    Test->set_timeout(60);
    Test->repl->change_master(1, 0);
    Test->tprintf("executing 3 INSERTs\n");
    for (int i = 0; i++; i < 3)
    {
        Test->set_timeout(60);
        sprintf(sql, "INSERT INTO t1 (x1, fl) VALUES(%d, 2);", i);
        Test->tprintf("Trying: %d\n", i);
        execute_query(Test->conn_rwsplit, sql);
    }
    Test->set_timeout(60);
    Test->tprintf("executing SELECT\n");
    execute_query(Test->conn_rwsplit, (char *) "SELECT * FROM t1;");

    Test->close_rwsplit();

    /** Sleep for at least one monitor interval */
    Test->tprintf("Waiting for monitor to detect changes\n");
    Test->stop_timeout();
    sleep(3);

    Test->set_timeout(60);
    Test->connect_rwsplit();
    Test->tprintf("Reconnecting and executing SELECT again\n");
    Test->set_timeout(60);
    Test->try_query(Test->conn_rwsplit, (char *) "SELECT * FROM t1;");

    Test->tprintf("Changing master back to node 0\n");
    Test->set_timeout(60);
    Test->repl->change_master(0, 1);
    Test->set_timeout(60);
    Test->repl->close_connections();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
