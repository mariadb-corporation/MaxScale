/**
 * @file mx321.cpp regression case for bug MXS-321: https://mariadb.atlassian.net/browse/MXS-321
 * 
 *
 * - Set max_connections to 100
 * - Create 200 connections
 * - Close connections
 * - Check that maxadmin list servers shows 0 connections
 */

#include <my_config.h>
#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include "testconnections.h"
#include "maxadmin_operations.h"

using namespace std;

#define CONNECTIONS 200

void create_and_check_connections(TestConnections* test, int target)
{
    MYSQL* stmt[CONNECTIONS];

    for(int i=0;i<CONNECTIONS;i++)
    {
        test->set_timeout(20);
        switch(target)
        {
            case 1:
            stmt[i] = test->open_rwsplit_connection();
            break;

            case 2:
            stmt[i] = test->open_readconn_master_connection();
            break;

            case 3:
            stmt[i] = test->open_readconn_master_connection();
            break;
        }
    }

    for(int i=0;i<CONNECTIONS;i++)
    {
        test->set_timeout(20);
        if(stmt[i])
            mysql_close(stmt[i]);
    }

    test->stop_timeout();
    sleep(10);
    char result[1024];
    char cmd[1024];
    int result_d;

    for (int j = 0; j < test->repl->N; j++)
    {
        test->set_timeout(30);
        sprintf(cmd, "show server server%d", j+1);
        test->add_result(get_maxadmin_param(test->maxscale_IP, (char*) "admin", test->maxadmin_password, cmd, (char*) "Current no. of conns:", result), "maxadmin command %s failed\n", cmd);
        result_d = 999;
        sscanf(result, "%d", &result_d);
        if (strlen(result) == 0)
        {
            test->add_result(1, "Empty Current no. of conns \n");
        }
        test->tprintf("result %s\t result_d %d\n", result, result_d);
        test->add_result(result_d, "Expected 0 connections, but got %d\n", result_d);
    }
}

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(50);

    Test->repl->execute_query_all_nodes((char *) "SET GLOBAL max_connections=100");
    Test->connect_maxscale();
    execute_query(Test->conn_rwsplit, "SET GLOBAL max_connections=100");
    Test->close_maxscale_connections();
    Test->stop_timeout();

    /** Create connections to readwritesplit */
    create_and_check_connections(Test, 1);

    /** Create connections to readconnroute master */
    create_and_check_connections(Test, 2);

    /** Create connections to readconnroute slave */
    create_and_check_connections(Test, 3);

    Test->repl->flush_hosts();

    Test->copy_all_logs();
    return(Test->global_result);
}
