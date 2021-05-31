/**
 * @file change_user.cpp mysql_change_user test
 *
 * - using RWSplit and user 'skysql': GRANT SELECT ON test.* TO user@'%'  identified by 'pass2';  FLUSH
 * PRIVILEGES;
 * - create a new connection to RSplit as 'user'
 * - try INSERT expecting 'access denied'
 * - call mysql_change_user() to change user to 'skysql'
 * - try INSERT again expecting success
 * - try to execute mysql_change_user() to switch to user 'user' but use rong password (expecting access
 * denied)
 * - try INSERT again expecting success (user should not be changed)
 */

#include <maxtest/testconnections.hh>

void run_test(TestConnections& test, MYSQL* conn)
{
    test.expect(mysql_change_user(conn, "user", "pass2", "test") == 0,
                "changing user failed: %s", mysql_error(conn));

    test.expect(execute_query_silent(conn, "INSERT INTO t1 VALUES (77, 11);") != 0,
                "INSERT query succeeded without INSERT privilege");

    test.expect(mysql_change_user(conn,
                                  test.repl->user_name.c_str(), test.repl->password.c_str(), "test") == 0,
                "changing user failed: %s", mysql_error(conn));

    test.expect(execute_query_silent(conn, "INSERT INTO t1 VALUES (77, 11);") == 0,
                "INSERT query succeeded without INSERT privilege");


    test.expect(mysql_change_user(conn, "user", "wrong_pass2", "test") != 0,
                "changing user with wrong password successed!");

    test.expect(strstr(mysql_error(conn), "Access denied for user"),
                "Wrong error message returned on failed authentication");

    test.expect(execute_query_silent(conn, "INSERT INTO t1 VALUES (77, 11);") == 0,
                "MaxScale should not disconnect on COM_CHANGE_USER failure");
}

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);

    test.repl->connect();
    execute_query(test.repl->nodes[0], "DROP USER 'user'@'%%'");
    test.try_query(test.repl->nodes[0], "CREATE USER user@'%%' identified by 'pass2'");
    test.try_query(test.repl->nodes[0], "GRANT SELECT ON test.* TO user@'%%'");
    test.try_query(test.repl->nodes[0], "FLUSH PRIVILEGES;");
    test.try_query(test.repl->nodes[0], "DROP TABLE IF EXISTS t1");
    test.try_query(test.repl->nodes[0], "CREATE TABLE t1 (x1 int, fl int)");
    test.repl->sync_slaves();
    test.repl->disconnect();

    test.maxscale->connect();
    test.tprintf("Testing readwritesplit");
    run_test(test, test.maxscale->conn_rwsplit[0]);
    test.tprintf("Testing readconnroute");
    run_test(test, test.maxscale->conn_master);
    test.maxscale->disconnect();

    // Test MXS-3366.
    test.maxscale->connect_rwsplit("");
    auto conn = test.maxscale->conn_rwsplit[0];
    test.expect(mysql_change_user(conn, "user", "pass2", "test") == 0,
                "changing user without CLIENT_CONNECT_WITH_DB-flag failed: %s", mysql_error(conn));
    test.maxscale->disconnect();

    test.repl->connect();
    execute_query_silent(test.repl->nodes[0], "DROP USER user@'%%';");
    execute_query_silent(test.repl->nodes[0], "DROP TABLE test.t1");
    test.repl->disconnect();
    return test.global_result;
}
