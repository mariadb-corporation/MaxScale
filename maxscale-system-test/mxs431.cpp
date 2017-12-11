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
    char str[256];
    int iterations = 100;
    test.repl->execute_query_all_nodes((char *) "set global max_connections = 600;");
    test.set_timeout(30);
    test.repl->stop_slaves();
    test.set_timeout(30);
    test.restart_maxscale();
    test.set_timeout(30);
    test.repl->connect();
    test.stop_timeout();

    /** Create a database on each node */
    for (int i = 0; i < test.repl->N; i++)
    {
        test.set_timeout(20);
        sprintf(str, "DROP DATABASE IF EXISTS shard_db%d", i);
        test.tprintf("%s\n", str);
        execute_query(test.repl->nodes[i], str);
        test.set_timeout(20);
        sprintf(str, "CREATE DATABASE shard_db%d", i);
        test.tprintf("%s\n", str);
        execute_query(test.repl->nodes[i], str);
        test.stop_timeout();
    }

    test.repl->close_connections();

    for (int j = 0; j < iterations && test.global_result == 0; j++)
    {
        for (int i = 0; i < test.repl->N && test.global_result == 0; i++)
        {
            sprintf(str, "shard_db%d", i);
            test.set_timeout(30);
            MYSQL *conn = open_conn_db(test.rwsplit_port, test.maxscale_IP,
                                       str, test.maxscale_user,
                                       test.maxscale_password, test.ssl);
            test.set_timeout(30);
            test.add_result(execute_query(conn, "SELECT 1"), "Trying DB %d failed at %d", i, j);
            mysql_close(conn);
        }
    }

    return test.global_result;
}
