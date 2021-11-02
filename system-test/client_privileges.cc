/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
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

void test_logins(TestConnections& test, bool expect_success);
void test_main(TestConnections& test);

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    test.repl->connect();

    auto& mxs = *test.maxscale;
    auto conn = mxs.open_rwsplit_connection2_nodb();

    auto create_user = [&](const char* user, const char* pass) {
            conn->cmd_f("DROP USER IF EXISTS '%s'@'%%'", user);
            conn->cmd_f("CREATE USER '%s'@'%%' IDENTIFIED BY '%s';", user, pass);
        };

    create_user(db_user, db_pass);
    create_user(table_user, table_pass);
    create_user(column_user, column_pass);
    create_user(process_user, process_pass);
    create_user(table_insert_user, table_insert_pass);
    test.repl->sync_slaves();

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
        test.repl->sync_slaves();

        if (test.ok())
        {
            // Check that logging in fails, as none of the users have privs.
            test_logins(test, false);
        }

        if (test.ok())
        {
            const char grant_fmt[] = "GRANT SELECT %s TO '%s'@'%%';";

            string db_grant = mxb::string_printf("ON %s.*", db);
            conn->cmd_f(grant_fmt, db_grant.c_str(), db_user);

            string table_grant = mxb::string_printf("ON %s", table);
            conn->cmd_f(grant_fmt, table_grant.c_str(), table_user);

            string column_grant = mxb::string_printf("(c2) ON %s", table);
            conn->cmd_f(grant_fmt, column_grant.c_str(), column_user);

            conn->cmd_f("GRANT EXECUTE ON PROCEDURE %s TO '%s'@'%%';", proc, process_user);

            conn->cmd_f("GRANT INSERT ON %s TO '%s'@'%%';", table, table_insert_user);
            test.repl->sync_slaves();
        }

        if (test.ok())
        {
            // Restart MaxScale to reload users.
            mxs.restart();
            mxs.wait_for_monitor();

            test_logins(test, true);
        }
        conn->cmd_f("DROP DATABASE %s;", db);
    }

    const char drop_fmt[] = "DROP USER '%s'@'%%';";
    conn->cmd_f(drop_fmt, db_user);
    conn->cmd_f(drop_fmt, table_user);
    conn->cmd_f(drop_fmt, column_user);
    conn->cmd_f(drop_fmt, process_user);
    conn->cmd_f(drop_fmt, table_insert_user);
}

void test_logins(TestConnections& test, bool expect_success)
{
    int port = test.maxscale->rwsplit_port;
    auto ip = test.maxscale->ip4();

    auto test_user = [&](const string& user, const string& pass, const string& query) {
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
