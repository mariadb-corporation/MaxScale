/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
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

void test_login(TestConnections& test, const string& user, const string& pass, const string& query,
                bool expected);

void test_main(TestConnections& test);

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto conn = test.maxscale->open_rwsplit_connection2_nodb();

    auto maybe_drop_user = [&](const char* user) {
        conn->cmd_f("DROP USER IF EXISTS '%s'@'%%'", user);
    };

    maybe_drop_user(db_user);
    maybe_drop_user(table_user);
    maybe_drop_user(column_user);
    maybe_drop_user(process_user);
    maybe_drop_user(table_insert_user);

    // Create a database, a table, a column and a stored procedure.
    conn->cmd_f("CREATE OR REPLACE DATABASE %s;", db);
    conn->cmd_f("CREATE TABLE %s (c1 INT, c2 INT);", table);
    conn->cmd_f("INSERT INTO %s VALUES (1, 2);", table);
    conn->cmd_f("CREATE PROCEDURE %s () "
                "BEGIN "
                "SELECT rand(); "
                "END; ",
                proc);
    test.repl->sync_slaves();

    if (test.ok())
    {
        test.tprintf("Database and table created.");

        // None of the users have been created so login should fail.
        test_login(test, db_user, db_pass, "", false);
        test_login(test, table_user, table_pass, "", false);
        test_login(test, column_user, column_pass, "", false);
        test_login(test, process_user, process_pass, "", false);
        test_login(test, table_insert_user, table_insert_pass, "", false);

        if (test.ok())
        {
            const char query_fmt[] = "SELECT %s from %s;";
            string select_query = mxb::string_printf(query_fmt, "*", table);
            {
                // Test db_user.
                auto db_scopeuser = conn->create_user(db_user, "%", db_pass);
                sleep(1);
                test_login(test, db_user, db_pass, select_query, false);
                // Add grant, login should work.
                db_scopeuser.grant_f("SELECT ON %s.*", db);
                sleep(1);
                test_login(test, db_user, db_pass, select_query, true);
            }

            {
                // Test table_user.
                auto table_scopeuser = conn->create_user(table_user, "%", table_pass);
                sleep(1);
                test_login(test, table_user, table_pass, select_query, false);
                // Add grant, login should work.
                table_scopeuser.grant_f("SELECT ON %s", table);
                sleep(1);
                test_login(test, table_user, table_pass, select_query, true);
            }

            {
                // Test column_user.
                auto column_scopeuser = conn->create_user(column_user, "%", column_pass);
                sleep(1);
                string col_select_query = mxb::string_printf(query_fmt, "c2", table);
                test_login(test, column_user, column_pass, col_select_query, false);
                // Add grant, login should work.
                column_scopeuser.grant_f("SELECT (c2) ON %s", table);
                sleep(1);
                test_login(test, column_user, column_pass, col_select_query, true);
            }

            {
                // Test process_user.
                auto process_scopeuser = conn->create_user(process_user, "%", process_pass);
                sleep(1);
                string call_query = mxb::string_printf("CALL %s();", proc);
                test_login(test, process_user, process_pass, call_query, false);
                // Add grant, login should work.
                process_scopeuser.grant_f("EXECUTE ON PROCEDURE %s", proc);
                sleep(1);
                test_login(test, process_user, process_pass, call_query, true);
            }

            {
                // Test table_insert_user.
                auto table_insert_scopeuser = conn->create_user(table_insert_user, "%", table_insert_pass);
                sleep(1);
                string insert_query = mxb::string_printf(
                    "INSERT INTO %s VALUES (1000 * rand(), 1000 * rand());", table);
                test_login(test, table_insert_user, table_insert_pass, insert_query, false);
                // Add grant, login should work.
                table_insert_scopeuser.grant_f("INSERT ON %s", table);
                sleep(1);
                test_login(test, table_insert_user, table_insert_pass, insert_query, true);
            }


            if (test.ok())
            {
                auto& repl = *test.repl;
                auto& mxs = *test.maxscale;

                // All ok so far. Test user account refreshing. First, generate a user not yet
                // known to MaxScale.
                auto master_conn = test.repl->backend(0)->open_connection();
                auto new_scopeuser = master_conn->create_user(new_user, "%", new_pass);
                new_scopeuser.grant_f("SELECT ON %s.*", db);
                sleep(1);

                // Should be able to login and query without reloading users.
                test_login(test, new_user, new_pass, "sElEcT rand();", true);

                // Change the password of the user. Login again with old password. Should work,
                // although the query should fail. Tests MXS-3630.
                master_conn->cmd_f("ALTER USER '%s' identified by '%s';", new_user, "different_pass");
                sleep(1);

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

void test_login(TestConnections& test, const string& user, const string& pass, const string& query,
                bool expected)
{
    bool login_ok = false;
    bool query_ok = false;
    int port = test.maxscale->rwsplit_port;
    auto ip = test.maxscale->ip4();

    MYSQL* conn = open_conn_db(port, ip, db, user, pass);
    if (mysql_errno(conn) == 0)
    {
        login_ok = true;
        if (query.empty() || test.try_query(conn, "%s", query.c_str()) == 0)
        {
            query_ok = true;
        }
    }
    mysql_close(conn);

    if (expected)
    {
        if (login_ok && query_ok)
        {
            test.tprintf("Login and/or query for user %s succeeded as expected.", user.c_str());
        }
        else
        {
            test.add_failure("Login or query for user %s failed when success was expected.", user.c_str());
        }
    }
    else
    {
        // If failure is expected, then even a partial success is a test fail.
        const char err_fmt[] = "%s for user %s succeeded when failure was expected.";
        if (!login_ok)
        {
            test.tprintf("Login for user %s failed as expected.", user.c_str());
        }
        else
        {
            test.add_failure(err_fmt, "Login", user.c_str());
            if (!query.empty())
            {
                test.expect(!query_ok, err_fmt, "Query", user.c_str());
            }
        }
    }
}
