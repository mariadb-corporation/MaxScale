/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
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
    auto create_user = [&](const char* user, const char* pass) {
        conn->cmd_f("CREATE USER '%s'@'%%' IDENTIFIED BY '%s';", user, pass);
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
            const char grant_fmt[] = "GRANT SELECT %s TO '%s'@'%%';";
            const char drop_fmt[] = "DROP USER '%s'@'%%';";

            // Test db_user.
            create_user(db_user, db_pass);
            string select_query = mxb::string_printf(query_fmt, "*", table);
            sleep(1);
            test_login(test, db_user, db_pass, select_query, false);
            // Add grant, login should work.
            string db_grant = mxb::string_printf("ON %s.*", db);
            conn->cmd_f(grant_fmt, db_grant.c_str(), db_user);
            sleep(1);
            test_login(test, db_user, db_pass, select_query, true);
            conn->cmd_f(drop_fmt, db_user);

            // Test table_user.
            create_user(table_user, table_pass);
            sleep(1);
            test_login(test, table_user, table_pass, select_query, false);
            // Add grant, login should work.
            string table_grant = mxb::string_printf("ON %s", table);
            conn->cmd_f(grant_fmt, table_grant.c_str(), table_user);
            sleep(1);
            test_login(test, table_user, table_pass, select_query, true);
            conn->cmd_f(drop_fmt, table_user);

            // Test column_user.
            create_user(column_user, column_pass);
            string col_select_query = mxb::string_printf(query_fmt, "c2", table);
            sleep(1);
            test_login(test, column_user, column_pass, col_select_query, false);
            // Add grant, login should work.
            string column_grant = mxb::string_printf("(c2) ON %s", table);
            conn->cmd_f(grant_fmt, column_grant.c_str(), column_user);
            sleep(1);
            test_login(test, column_user, column_pass, col_select_query, true);
            conn->cmd_f(drop_fmt, column_user);

            // Test process_user.
            create_user(process_user, process_pass);
            string call_query = mxb::string_printf("CALL %s();", proc);
            sleep(1);
            test_login(test, process_user, process_pass, call_query, false);
            // Add grant, login should work.
            conn->cmd_f("GRANT EXECUTE ON PROCEDURE %s TO '%s'@'%%';", proc, process_user);
            sleep(1);
            test_login(test, process_user, process_pass, call_query, true);
            conn->cmd_f(drop_fmt, process_user);

            // Test table_insert_user.
            create_user(table_insert_user, table_insert_pass);
            string insert_query = mxb::string_printf("INSERT INTO %s VALUES (1000 * rand(), 1000 * rand());",
                                                     table);
            sleep(1);
            test_login(test, table_insert_user, table_insert_pass, insert_query, false);
            // Add grant, login should work.
            conn->cmd_f("GRANT INSERT ON %s TO '%s'@'%%';", table, table_insert_user);
            sleep(1);
            test_login(test, table_insert_user, table_insert_pass, insert_query, true);
            conn->cmd_f(drop_fmt, table_insert_user);
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
