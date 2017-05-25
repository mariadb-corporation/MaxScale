/**
 * @file mxs419_lots_of_connections.cpp ("Socket creation failed due 24, Too many open files")
 * Trying to create 500 connections to every router
 * checks Maxscale is alive
 */



#include "testconnections.h"
int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int connections = 500;

    Test->set_timeout(30);
    Test->tprintf("set global max_connections = 2000");
    Test->connect_maxscale();
    Test->try_query(Test->conn_rwsplit, (char *) "set global max_connections = 2000;\n");
    Test->close_maxscale_connections();

    Test->tprintf("Trying %d connections\n", connections);
    Test->add_result(Test->create_connections(connections, true, true, true, false),
                     "Error creating connections\n");

    Test->set_timeout(30);
    Test->tprintf("set global max_connections = 100");
    Test->connect_maxscale();
    Test->try_query(Test->conn_rwsplit, (char *) "set global max_connections = 100;\n");
    Test->close_maxscale_connections();

    Test->tprintf("Checking if Maxscale alive\n");
    Test->check_maxscale_alive();
    int rval = Test->global_result;
    delete Test;

    return rval;
}
