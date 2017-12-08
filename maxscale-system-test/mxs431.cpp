/**
 * @file mxs431.cpp Bug regression test case for MXS-431: ("Backend authentication fails with schemarouter")
 *
 * - Create database 'testdb' on one node
 * - Connect repeatedly to MaxScale with 'testdb' as the default database and execute SELECT 1
 */


#include <iostream>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    char str[256];
    int iterations = Test->smoke ? 100 : 500;
    Test->repl->execute_query_all_nodes((char *) "set global max_connections = 600;");
    Test->set_timeout(30);
    Test->repl->stop_slaves();
    Test->set_timeout(30);
    Test->maxscales->restart_maxscale(0);
    Test->set_timeout(30);
    Test->repl->connect();
    Test->stop_timeout();

    /** Create a database on each node */
    for (int i = 0; i < Test->repl->N; i++)
    {
        Test->set_timeout(20);
        sprintf(str, "DROP DATABASE IF EXISTS shard_db%d", i);
        Test->tprintf("%s\n", str);
        execute_query(Test->repl->nodes[i], str);
        Test->set_timeout(20);
        sprintf(str, "CREATE DATABASE shard_db%d", i);
        Test->tprintf("%s\n", str);
        execute_query(Test->repl->nodes[i], str);
        Test->stop_timeout();
    }

    Test->repl->close_connections();

    for (int j = 0; j < iterations && Test->global_result == 0; j++)
    {
        for (int i = 0; i < Test->repl->N; i++)
        {
            sprintf(str, "shard_db%d", i);
            Test->set_timeout(15);
            MYSQL *conn = open_conn_db(Test->maxscales->rwsplit_port[0], Test->maxscales->IP[0],
                                       str, Test->maxscales->user_name,
                                       Test->maxscales->password, Test->ssl);
            Test->set_timeout(15);
            Test->tprintf("Trying DB %d\n", i);
            if (execute_query(conn, "SELECT 1"))
            {
                Test->add_result(1, "Failed at %d\n", j);
                break;
            }
            mysql_close(conn);
        }
    }
    int rval = Test->global_result;
    delete Test;
    return rval;
}
