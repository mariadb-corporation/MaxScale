#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;


int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->repl->connect();

    printf("Connecting to RWsplit\n");
    Test->connect_rwsplit();

    Test->add_result(create_t1(Test->conn_rwsplit), "Error creating 't1'\n");

    Test->try_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 (x1, fl) VALUES(0, 1);");
    Test->tprintf("Changing master to node 1\n");
    Test->repl->change_master(1, 0);
    Test->tprintf("executing 3 INSERTs\n");
    execute_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 (x1, fl) VALUES(0, 2);");
    execute_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 (x1, fl) VALUES(1, 2);");
    execute_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 (x1, fl) VALUES(2, 2);");
    Test->tprintf("executing SELECT\n");
    execute_query(Test->conn_rwsplit, (char *) "SELECT * FROM t1;");

    Test->close_rwsplit();
    Test->connect_rwsplit();
    Test->tprintf("Reconnecting and executing SELECT again\n");
    Test->try_query(Test->conn_rwsplit, (char *) "SELECT * FROM t1;");


    Test->tprintf("Changing master back to node 0\n");
    Test->repl->change_master(0, 1);

    Test->repl->close_connections();

    Test->copy_all_logs(); return(Test->global_result);
}
