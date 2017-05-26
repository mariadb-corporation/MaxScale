/**
  * @file connect_to_nonexisting_db.cpp Tries to connect to non existing DB, expects no crash
  */

// some relations to bug#425

#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(30);

    Test->tprintf("Connecting to RWSplit\n");
    Test->conn_rwsplit = open_conn_no_db(Test->rwsplit_port, Test->maxscale_IP, Test->maxscale_user,
                                         Test->maxscale_password, Test->ssl);
    if (Test->conn_rwsplit == NULL)
    {
        Test->add_result(1, "Error connecting to MaxScale\n");
        delete Test;
        return 1;
    }
    Test->tprintf("Removing 'test' DB\n");
    execute_query(Test->conn_rwsplit, (char *) "DROP DATABASE IF EXISTS test;");
    Test->tprintf("Closing connections and waiting 5 seconds\n");
    Test->close_rwsplit();
    sleep(5);

    Test->tprintf("Connection to non-existing DB (all routers)\n");
    Test->connect_maxscale();
    Test->close_maxscale_connections();

    Test->tprintf("Connecting to RWSplit again to recreate 'test' db\n");
    Test->conn_rwsplit = open_conn_no_db(Test->rwsplit_port, Test->maxscale_IP, Test->maxscale_user,
                                         Test->maxscale_password, Test->ssl);
    if (Test->conn_rwsplit == NULL)
    {
        printf("Error connecting to MaxScale\n");
        delete Test;
        return 1;
    }

    Test->tprintf("Creating and selecting 'test' DB\n");
    Test->try_query(Test->conn_rwsplit, (char *) "CREATE DATABASE test; USE test");
    Test->tprintf("Creating 't1' table\n");
    Test->add_result(create_t1(Test->conn_rwsplit), "Error creation 't1'\n");
    Test->close_rwsplit();

    Test->tprintf("Reconnectiong\n");
    Test->add_result(Test->connect_maxscale(), "error connection to Maxscale\n");
    Test->tprintf("Trying simple operations with t1 \n");
    Test->try_query(Test->conn_rwsplit, (char *) "INSERT INTO t1 (x1, fl) VALUES(0, 1);");
    Test->set_timeout(240);
    Test->add_result(execute_select_query_and_check(Test->conn_rwsplit, (char *) "SELECT * FROM t1;", 1),
                     "Error execution SELECT * FROM t1;\n");

    Test->close_maxscale_connections();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
