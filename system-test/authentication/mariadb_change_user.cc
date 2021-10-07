/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

void run_test(TestConnections& test, MYSQL* conn)
{
    test.expect(mysql_change_user(conn, "user", "pass2", "test") == 0,
                "changing user failed: %s", mysql_error(conn));

    test.expect(execute_query_silent(conn, "INSERT INTO t1 VALUES (77, 11);") != 0,
                "INSERT query succeeded without INSERT privilege");

    test.expect(mysql_change_user(conn, test.repl->user_name().c_str(), test.repl->password().c_str(),
                                  "test") == 0,
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
