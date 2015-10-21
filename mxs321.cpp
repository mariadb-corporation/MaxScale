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

using namespace std;

#define CONNECTIONS 200

void create_and_check_connections(TestConnections* test, int target)
{
    MYSQL* stmt[CONNECTIONS];

    for(int i=0;i<CONNECTIONS;i++)
    {
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
        if(stmt[i])
            mysql_close(stmt[i]);
    }

    sleep(10);

    char* result = test->execute_ssh_maxscale((char*)"maxadmin list servers|grep 'server[0-9]'|cut -d '|' -f 4|tr -d ' '|uniq");
    
    if(strcmp(result, "0") != 0)
    {
        cout << "Test failed: Expected 0 connections:" << result << endl;
        test->global_result++;
    }
}

int main(int argc, char *argv[])
{

    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(50);

    Test->connect_maxscale();
    execute_query(Test->conn_rwsplit, "SET GLOBAL max_connections=100");
    Test->close_maxscale_connections();

    /** Create connections to readwritesplit */
    create_and_check_connections(Test, 1);

    /** Create connections to readconnroute master */
    create_and_check_connections(Test, 2);

    /** Create connections to readconnroute slave */
    create_and_check_connections(Test, 3);

    Test->copy_all_logs();
    return(Test->global_result);
}
