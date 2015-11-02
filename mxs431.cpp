/**
 * @file mxs431.cpp Bug regression test case for MXS-431: https://mariadb.atlassian.net/browse/MXS-431
 *
 * - Create database 'testdb' on one node
 * - Connect repeatedly to MaxScale with 'testdb' as the default database and execute SELECT 1
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    char str[256];
    int iterations = Test->smoke ? 100 : 500;
    Test->set_timeout(30);
    Test->repl->stop_slaves();
    Test->restart_maxscale();
    Test->repl->connect();
    Test->stop_timeout();

    /** Create a database on each node */
    for (int i = 0; i < Test->repl->N; i++) {
        Test->set_timeout(10);
        sprintf(str, "DROP DATABASE IF EXISTS shard_db%d", i);
        Test->tprintf("%s\n", str);
        execute_query(Test->repl->nodes[i], str);

        sprintf(str, "CREATE DATABASE shard_db%d", i);
        Test->tprintf("%s\n", str);
        execute_query(Test->repl->nodes[i], str);
        Test->stop_timeout();
    }

    Test->repl->close_connections();

    for(int j = 0;j < iterations && Test->global_result == 0; j++)
    {
        for (int i = 0; i < Test->repl->N; i++) {        
            sprintf(str, "shard_db%d", i);
            Test->set_timeout(5);
            MYSQL *conn = open_conn_db(Test->rwsplit_port, Test->maxscale_IP, 
                                       str, Test->maxscale_user,
                                       Test->maxscale_password, Test->ssl);
            if(execute_query(conn, "SELECT 1"))
            {
                Test->add_result(1, "Failed at %d", j);
                break;
            }
            mysql_close(conn);
        }
    }
    Test->copy_all_logs();
    return(Test->global_result);
}

