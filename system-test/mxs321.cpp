/**
 * @file mx321.cpp regression case for bug MXS-321 ("Incorrect number of connections in maxadmin list view")
 *
 * - Set max_connections to 100
 * - Create 200 connections
 * - Close connections
 * - Check that maxadmin list servers shows 0 connections
 */


#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include "testconnections.h"
#include "maxadmin_operations.h"

using namespace std;

#define CONNECTIONS 200
int check_connection_count(TestConnections* test, int server)
{
    char result[1024];
    char cmd[1024];
    test->set_timeout(30);
    sprintf(cmd, "show server server%d", server);
    test->add_result(test->maxscales->get_maxadmin_param(0, cmd, (char*) "Current no. of conns:", result),
                     "maxadmin command %s failed\n",
                     cmd);
    int result_d = 999;
    sscanf(result, "%d", &result_d);
    if (strlen(result) == 0)
    {
        test->add_result(1, "Empty Current no. of conns \n");
    }
    test->tprintf("result %s\t result_d %d\n", result, result_d);
    return result_d;
}

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
    int result_d;

    for (int j = 1; j < test->repl->N + 1; j++)
    {
        if ((result_d = check_connection_count(test, j)))
        {
            test->tprintf("Waiting 5 seconds and testing again.");
            sleep(5);
            result_d = check_connection_count(test, j);
        }

        test->add_result(result_d, "Expected 0 connections, but got %d\n", result_d);
    }
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

    Test->repl->flush_hosts();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
