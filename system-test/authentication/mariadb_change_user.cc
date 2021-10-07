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
#include <maxtest/mariadb_connector.hh>

using std::string;
void test_main(TestConnections& test);
void test_connection(TestConnections& test, MYSQL* conn);

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& repl = *test.repl;
    auto server_conn = repl.backend(0)->open_connection();

    auto user = server_conn->create_user("user", "%", "pass2");
    user.grant("select on test.*");
    server_conn->cmd("flush privileges;");

    auto table = server_conn->create_table("test.t1", "x1 int, fl int");

    repl.sync_slaves();
    auto& mxs = *test.maxscale;

    mxs.connect();
    test.tprintf("Testing readwritesplit");
    test_connection(test, mxs.conn_rwsplit[0]);
    test.tprintf("Testing readconnroute");
    test_connection(test, mxs.conn_master);
    mxs.disconnect();

    // Test MXS-3366.
    mxs.connect_rwsplit("");
    auto rwsplit_conn = mxs.conn_rwsplit[0];
    test.expect(mysql_change_user(rwsplit_conn, "user", "pass2", "test") == 0,
                "changing user without CLIENT_CONNECT_WITH_DB-flag failed: %s", mysql_error(rwsplit_conn));
    mxs.disconnect();

    // Log in as userA. Change password of userB on backend, then try "change user". MaxScale is using old
    // user account data and accepts the command. In the end, change user should fail but session should
    // remain open.
    const string unA = "userA";
    const string pwA = "passA";
    const string unB = "userB";
    const string pwB = "passB";
    const string select_user = "select current_user()";

    auto user_A = server_conn->create_user(unA, "%", pwA);
    auto user_B = server_conn->create_user(unB, "%", pwB);

    auto connA = mxs.try_open_rwsplit_connection(unA, pwA);
    auto connB = mxs.try_open_rwsplit_connection(unB, pwB);
    test.expect(connA->is_open() && connB->is_open(), "Login failed");

    if (test.ok())
    {
        const char expected[] = "userA@%";
        const char errmsg_fmt[] = "Wrong user. Got '%s', expected '%s'.";
        string orig_user = connA->simple_query(select_user);
        test.expect(orig_user == expected, errmsg_fmt, orig_user.c_str(), expected);

        server_conn->cmd_f("alter user '%s' identified by '%s';", unB.c_str(), "passC");
        if (test.ok())
        {
            test.tprintf("Password changed on server, trying COM_CHANGE_USER.");
            bool user_changed = connA->change_user(unB, pwB, "");
            test.expect(!user_changed, "Change user succeeded when it should have failed.");
            string curr_user = connA->simple_query("select current_user()");
            test.expect(curr_user == expected, "Wrong user. Got '%s', expected '%s'.",
                        curr_user.c_str(), expected);
        }
    }
}

void test_connection(TestConnections& test, MYSQL* conn)
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
                "changing user with wrong password succeeded!");

    test.expect(strstr(mysql_error(conn), "Access denied for user"),
                "Wrong error message returned on failed authentication");

    test.expect(execute_query_silent(conn, "INSERT INTO t1 VALUES (77, 11);") == 0,
                "MaxScale should not disconnect on COM_CHANGE_USER failure");
}
