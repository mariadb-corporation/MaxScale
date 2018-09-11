/**
 * MXS-1947: Composite roles are not supported
 *
 * https://jira.mariadb.org/browse/MXS-1947
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.repl->connect();

    auto prepare =
    {
        "DROP USER test@'%'",
        "CREATE USER test@'%' IDENTIFIED BY 'test';",
        "CREATE ROLE a;",
        "CREATE ROLE b;",
        "CREATE DATABASE db;",
        "GRANT ALL ON db.* TO a;",
        "GRANT a TO b;",
        "GRANT b TO test@'%';",
        "SET DEFAULT ROLE b FOR test@'%';"
    };

    for (auto a : prepare)
    {
        execute_query_silent(test.repl->nodes[0], a);
    }

    // Wait for the users to replicate
    test.repl->sync_slaves();

    test.tprintf("Connect with a user that has a composite role as the default role");
    MYSQL* conn = open_conn_db(test.maxscales->rwsplit_port[0], test.maxscales->IP[0], "db", "test", "test");
    test.assert(mysql_errno(conn) == 0, "Connection failed: %s", mysql_error(conn));
    mysql_close(conn);

    auto cleanup =
    {
        "DROP DATABASE IF EXISTS db;",
        "DROP ROLE IF EXISTS a;",
        "DROP ROLE IF EXISTS b;",
        "DROP USER 'test'@'%';"
    };

    for (auto a : cleanup)
    {
        execute_query_silent(test.repl->nodes[0], a);
    }

    return test.global_result;
}
