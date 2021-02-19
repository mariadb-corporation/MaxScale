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
        test->set_timeout(20);
        switch (target)
        {
        case 1:
            stmt[i] = test->maxscales->open_rwsplit_connection(0);
            break;

        case 2:
            stmt[i] = test->maxscales->open_readconn_master_connection(0);
            break;

        case 3:
            stmt[i] = test->maxscales->open_readconn_master_connection(0);
            break;
        }
    }

    for (int i = 0; i < CONNECTIONS; i++)
    {
        test->set_timeout(20);
        if (stmt[i])
        {
            mysql_close(stmt[i]);
        }
    }

    test->stop_timeout();
    sleep(10);

    test->check_current_connections(0, 0);
}

int main(int argc, char* argv[])
{

    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(50);

    Test->repl->execute_query_all_nodes((char*) "SET GLOBAL max_connections=100");
    Test->maxscales->connect_maxscale(0);
    execute_query(Test->maxscales->conn_rwsplit[0], "SET GLOBAL max_connections=100");
    Test->maxscales->close_maxscale_connections(0);
    Test->stop_timeout();

    /** Create connections to readwritesplit */
    create_and_check_connections(Test, 1);

    /** Create connections to readconnroute master */
    create_and_check_connections(Test, 2);

    /** Create connections to readconnroute slave */
    create_and_check_connections(Test, 3);

    // TODO: Just a simple flush_hosts() may be sufficient.
    Test->repl->prepare_for_test();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
