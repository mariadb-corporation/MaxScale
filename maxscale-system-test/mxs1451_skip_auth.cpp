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
    test.tprintf("Creating users");
    test.repl->connect();
    execute_query(test.repl->nodes[0], "CREATE USER 'auth_test'@'%s' IDENTIFIED BY 'test'",
                  test.maxscales->ip(0));
    execute_query(test.repl->nodes[0], "GRANT ALL ON *.* to 'auth_test'@'%s'", test.maxscales->ip(0));
    execute_query(test.repl->nodes[0], "CREATE USER 'auth_test_nopw'@'%s'", test.maxscales->ip(0));
    execute_query(test.repl->nodes[0], "GRANT ALL ON *.* to 'auth_test_nopw'@'%s'",
                  test.maxscales->ip(0));
    test.repl->sync_slaves();
    test.repl->close_connections();

    test.tprintf("Trying to connect through MaxScale");

    test.set_timeout(60);
    test.tprintf("... with correct credentials");
    MYSQL* conn = open_conn_db(test.maxscales->rwsplit_port[0], test.maxscales->ip(0), "test", "auth_test",
                               "test", false);
    test.try_query(conn, "SHOW DATABASES");
    mysql_close(conn);

    test.set_timeout(60);
    test.tprintf("... without a password");
    conn = open_conn_db(test.maxscales->rwsplit_port[0], test.maxscales->ip(0), "test", "auth_test_nopw", "",
                        false);
    test.try_query(conn, "SHOW DATABASES");
    mysql_close(conn);

    test.set_timeout(60);
    test.tprintf("... with wrong password");
    conn = open_conn_db(test.maxscales->rwsplit_port[0], test.maxscales->ip(0), "test", "auth_test",
                        "wrong_password", false);
    test.add_result(mysql_errno(conn) == 0, "Connection with wrong password should fail");
    mysql_close(conn);

    test.set_timeout(60);
    test.tprintf("... with a password for user without a password");
    conn = open_conn_db(test.maxscales->rwsplit_port[0], test.maxscales->ip(0), "test", "auth_test_nopw", "test",
                        false);
    test.add_result(mysql_errno(conn) == 0,
                    "Connection with wrong password to user without a password should fail");
    mysql_close(conn);

    test.tprintf("... with bad credentials");
    conn = open_conn_db(test.maxscales->rwsplit_port[0], test.maxscales->ip(0), "test", "wrong_user",
                        "wrong_password", false);
    test.add_result(mysql_errno(conn) == 0, "Connection with bad credentials should fail");
    mysql_close(conn);

    test.set_timeout(60);
    test.tprintf("Dropping users");
    test.repl->connect();
    execute_query(test.repl->nodes[0], "DROP USER 'auth_test'@'%s'", test.maxscales->ip(0));
    execute_query(test.repl->nodes[0], "DROP USER 'auth_test_nopw'@'%s'", test.maxscales->ip(0));
    test.repl->close_connections();

    return test.global_result;
}
