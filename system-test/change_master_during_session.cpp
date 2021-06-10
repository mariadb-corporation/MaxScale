/**
 * @file change_master_during_seesion.cpp Tries to reconfigure replication setup to use another node as a
 * Master
 * - connect to RWSplit
 * - reconfugure backend
 * - checks that after time > monitor_interval everything is ok
 */

#include <iostream>
#include <maxtest/testconnections.hh>
#include <maxtest/sql_t1.hh>

using namespace std;


int main(int argc, char* argv[])
{
    char sql[1024];
    TestConnections* Test = new TestConnections(argc, argv);
    Test->reset_timeout();

    Test->repl->connect();

    printf("Connecting to RWsplit\n");
    Test->maxscale->connect_rwsplit();
    Test->reset_timeout();
    Test->add_result(create_t1(Test->maxscale->conn_rwsplit[0]), "Error creating 't1'\n");

    Test->try_query(Test->maxscale->conn_rwsplit[0], (char*) "INSERT INTO t1 (x1, fl) VALUES(0, 1);");
    Test->tprintf("Changing master to node 1\n");
    Test->reset_timeout();
    Test->repl->change_master(1, 0);
    Test->tprintf("executing 3 INSERTs\n");
    for (int i = 0; i < 3; i++)
    {
        Test->reset_timeout();
        sprintf(sql, "INSERT INTO t1 (x1, fl) VALUES(%d, 2);", i);
        Test->tprintf("Trying: %d\n", i);
        execute_query(Test->maxscale->conn_rwsplit[0], "%s", sql);
    }
    Test->reset_timeout();
    Test->tprintf("executing SELECT\n");
    execute_query(Test->maxscale->conn_rwsplit[0], (char*) "SELECT * FROM t1;");

    Test->maxscale->close_rwsplit();

    /** Sleep for at least one monitor interval */
    Test->tprintf("Waiting for monitor to detect changes\n");
    sleep(3);

    Test->reset_timeout();
    Test->maxscale->connect_rwsplit();
    Test->tprintf("Reconnecting and executing SELECT again\n");
    Test->reset_timeout();
    Test->try_query(Test->maxscale->conn_rwsplit[0], (char*) "SELECT * FROM t1;");

    Test->tprintf("Changing master back to node 0\n");
    Test->reset_timeout();
    Test->repl->change_master(0, 1);
    Test->reset_timeout();
    Test->repl->close_connections();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
