/**
 * @file mx321.cpp regression case for bug MXS-321 ("Incorrect number of connections in list view")
 *
 * - Set max_connections to 100
 * - Create 200 connections
 * - Close connections
 * - Check that list servers shows 0 connections
 */


#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <maxtest/testconnections.hh>

using namespace std;

#define CONNECTIONS 200

void create_and_check_connections(TestConnections* test, int target)
{
    MYSQL* stmt[CONNECTIONS];

    for (int i = 0; i < CONNECTIONS; i++)
    {
        test->reset_timeout();
        switch (target)
        {
        case 1:
            stmt[i] = test->maxscale->open_rwsplit_connection();
            break;

        case 2:
            stmt[i] = test->maxscale->open_readconn_master_connection();
            break;

        case 3:
            stmt[i] = test->maxscale->open_readconn_master_connection();
            break;
        }
    }

    for (int i = 0; i < CONNECTIONS; i++)
    {
        test->reset_timeout();
        if (stmt[i])
        {
            mysql_close(stmt[i]);
        }
    }

    sleep(10);

    test->check_current_connections(0);
}

int main(int argc, char* argv[])
{

    TestConnections* Test = new TestConnections(argc, argv);
    Test->reset_timeout();

    Test->repl->execute_query_all_nodes((char*) "SET GLOBAL max_connections=100");
    Test->maxscale->connect_maxscale();
    execute_query(Test->maxscale->conn_rwsplit[0], "SET GLOBAL max_connections=100");
    Test->maxscale->close_maxscale_connections();

    /** Create connections to readwritesplit */
    create_and_check_connections(Test, 1);

    /** Create connections to readconnroute master */
    create_and_check_connections(Test, 2);

    /** Create connections to readconnroute slave */
    create_and_check_connections(Test, 3);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
