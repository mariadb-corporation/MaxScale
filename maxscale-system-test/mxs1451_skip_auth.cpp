/**
 * MXS-1451: Password is not stored with skip_authentication=true
 *
 * Check that connection through MaxScale work even if authentication is disabled
 */

#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    test.set_timeout(60);
    test.tprintf("Creating user...");
    test.repl->connect();
    execute_query(test.repl->nodes[0], "CREATE USER 'auth_test'@'%s' IDENTIFIED BY 'test'", test.maxscale_ip());
    execute_query(test.repl->nodes[0], "GRANT ALL ON *.* to 'auth_test'@'%s'", test.maxscale_ip());
    test.repl->sync_slaves();
    test.repl->close_connections();

    test.set_timeout(60);
    test.tprintf("Trying to connect through MaxScale");
    MYSQL* conn = open_conn_db(test.rwsplit_port, test.maxscale_ip(), "test", "auth_test", "test", false);
    test.try_query(conn, "SHOW DATABASES");
    mysql_close(conn);

    test.tprintf("Trying query with bad credentials");
    conn = open_conn_db(test.rwsplit_port, test.maxscale_ip(), "test", "wrong_user", "wrong_password", false);
    test.add_result(execute_query_silent(conn, "SHOW DATABASES") == 0, "Connection with bad credentials should fail");
    mysql_close(conn);

    test.set_timeout(60);
    test.tprintf("Dropping user");
    test.repl->connect();
    execute_query(test.repl->nodes[0], "DROP USER 'auth_test'@'%s'", test.maxscale_ip());
    test.repl->close_connections();

    return test.global_result;
}
