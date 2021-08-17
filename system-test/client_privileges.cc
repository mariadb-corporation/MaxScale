/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>
#include <maxtest/mariadb_connector.hh>

using std::string;

namespace
{
const char db[] = "priv_test";
const char table[] = "priv_test.t1";
const char proc[] = "priv_test.p1";

const char db_user[] = "db_user";
const char db_pass[] = "db_pass";
const char table_user[] = "table_user";
const char table_pass[] = "table_pass";
const char column_user[] = "column_user";
const char column_pass[] = "column_pass";
const char process_user[] = "process_user";
const char process_pass[] = "process_pass";
const char table_insert_user[] = "table_insert_user";
const char table_insert_pass[] = "table_insert_pass";
const char new_user[] = "new_user";
const char new_pass[] = "new_pass";
}

void test_logins(TestConnections& test, bool expect_success);
void test_main(TestConnections& test);
bool test_user_full(TestConnections& test, const string& ip, int port,
                    const string& user, const string& pass, const string& query);

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    auto conn = mxs.open_rwsplit_connection2();

    auto db_scopeuser = conn->create_user(db_user, "%", db_pass);
    auto table_scopeuser = conn->create_user(table_user, "%", table_pass);
    auto column_scopeuser = conn->create_user(column_user, "%", column_pass);
    auto process_scopeuser = conn->create_user(process_user, "%", process_pass);
    auto table_insert_scopeuser = conn->create_user(table_insert_user, "%", table_insert_pass);
    repl.sync_slaves();

    if (test.ok())
    {
        test.tprintf("Users created.");
        // Create a database, a table, a column and a stored procedure.
        conn->cmd_f("CREATE OR REPLACE DATABASE %s;", db);
        conn->cmd_f("CREATE TABLE %s (c1 INT, c2 INT);", table);
        conn->cmd_f("INSERT INTO %s VALUES (1, 2);", table);
        conn->cmd_f("CREATE PROCEDURE %s () "
                    "BEGIN "
                    "SELECT rand(); "
                    "END; ",
                    proc);
        repl.sync_slaves();

        if (test.ok())
        {
            // Check that logging in fails, as none of the users have privs.
            test_logins(test, false);
        }

        if (test.ok())
        {
            string db_grant = mxb::string_printf("SELECT ON %s.*", db);
            db_scopeuser.grant(db_grant);
            table_scopeuser.grant_f("SELECT ON %s", table);
            column_scopeuser.grant_f("SELECT (c2) ON %s", table);
            process_scopeuser.grant_f("EXECUTE ON PROCEDURE %s", proc);
            table_insert_scopeuser.grant_f("INSERT ON %s", table);
            repl.sync_slaves();

            if (test.ok())
            {
                // Restart MaxScale to reload users.
                mxs.restart();
                mxs.wait_for_monitor();

                test_logins(test, true);
            }

            if (test.ok())
            {
                // All ok so far. Test user account refreshing. First, generate a user not yet
                // known to MaxScale.
                auto master_conn = test.repl->backend(0)->open_connection();
                auto new_scopeuser = master_conn->create_user(new_user, "%", new_pass);
                new_scopeuser.grant(db_grant);
                repl.sync_slaves();
                // Should be able to login and query without reloading users.
                bool login_ok = test_user_full(test, mxs.ip(), mxs.rwsplit_port, new_user, new_pass,
                                               "sElEcT rand();");
                test.expect(login_ok, "Login to a new user failed.");

                // Change the password of the user. Login again with old password. Should work,
                // although the query should fail. Tests MXS-3630.
                master_conn->cmd_f("ALTER USER '%s' identified by '%s';", new_user, "different_pass");
                repl.sync_slaves();

                auto test_conn = mxs.try_open_rwsplit_connection(new_user, new_pass);
                test.expect(test_conn->is_open(), "Logging in with old password failed.");
                auto res = test_conn->try_query("select 1;");
                test.expect(!res, "Query succeeded when it should have failed.");

                // Wait a bit and try connecting again. Now even the connection should fail, as MaxScale
                // updated user accounts.
                sleep(1);
                test_conn = mxs.try_open_rwsplit_connection(new_user, new_pass);
                test.expect(!test_conn->is_open(), "Logging in with old password succeeded when it should "
                                                   "have failed.");
            }
        }

        conn->cmd_f("DROP DATABASE %s;", db);
    }
}

bool test_user_full(TestConnections& test, const string& ip, int port,
                    const string& user, const string& pass, const string& query)
{
    bool rval = false;
    MYSQL* conn = open_conn_db(port, ip, db, user, pass);
    if (mysql_errno(conn) == 0)
    {
        // Query should work.
        if (test.try_query(conn, "%s", query.c_str()) == 0)
        {
            rval = true;
        }
    }
    mysql_close(conn);
    return rval;
}

void test_logins(TestConnections& test, bool expect_success)
{
    int port = test.maxscale->rwsplit_port;
    auto ip = test.maxscale->ip4();

    auto test_user = [&](const string& user, const string& pass, const string& query) {
            return test_user_full(test, ip, port, user, pass, query);
        };


    const char query_fmt[] = "SELECT %s from %s;";
    string query = mxb::string_printf(query_fmt, "*", table);

    auto report_error = [&test](const char* user, bool result, bool expected) {
            const char* result_str = result ? "succeeded" : "failed";
            const char* expected_str = expected ? "success" : "failure";
            test.expect(result == expected, "User %s login and query %s when %s was expected.",
                        user, result_str, expected_str);
        };

    bool ret = test_user(db_user, db_pass, query);
    report_error(db_user, ret, expect_success);

    ret = test_user(table_user, table_pass, query);
    report_error(table_user, ret, expect_success);

    query = mxb::string_printf(query_fmt, "c2", table);
    ret = test_user(column_user, column_pass, query);
    report_error(column_user, ret, expect_success);

    query = mxb::string_printf("CALL %s();", proc);
    ret = test_user(process_user, process_pass, query);
    report_error(process_user, ret, expect_success);

    query = mxb::string_printf("INSERT INTO %s VALUES (1000 * rand(), 1000 * rand());", table);
    ret = test_user(table_insert_user, table_insert_pass, query);
    report_error(table_insert_user, ret, expect_success);
}
