/**
 * @file mxs431.cpp Bug regression test case for MXS-431: ("Backend authentication fails with schemarouter")
 *
 * - Create database 'testdb' on one node
 * - Connect repeatedly to MaxScale with 'testdb' as the default database and execute SELECT 1
 */

#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    test.repl->connect();

    /** Create a database on each node */
    for (int i = 0; i < test.repl->N; i++)
    {
        test.set_timeout(60);
        execute_query(test.repl->nodes[i], "set global max_connections = 600");
        execute_query(test.repl->nodes[i], "DROP DATABASE IF EXISTS shard_db%d", i);
        execute_query(test.repl->nodes[i], "CREATE DATABASE shard_db%d", i);
        test.stop_timeout();
    }

    int iterations = 100;

    for (int j = 0; j < iterations && test.global_result == 0; j++)
    {
        for (int i = 0; i < test.repl->N && test.global_result == 0; i++)
        {
            char str[256];
            sprintf(str, "shard_db%d", i);
            test.set_timeout(60);
            MYSQL *conn = open_conn_db(test.maxscales->rwsplit_port[0], test.maxscales->IP[0],
                                       str, test.maxscales->user_name,
                                       test.maxscales->password, test.ssl);
            test.set_timeout(60);
            test.add_result(execute_query(conn, "SELECT 1"), "Trying DB %d failed at %d", i, j);
            mysql_close(conn);
        }
    }

    /** Drop the databases */
    for (int i = 0; i < test.repl->N; i++)
    {
        test.set_timeout(60);
        execute_query(test.repl->nodes[i], "DROP DATABASE shard_db%d", i);
        test.stop_timeout();
    }

    return test.global_result;
}
