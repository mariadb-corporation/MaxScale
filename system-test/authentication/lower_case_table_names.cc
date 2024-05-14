/*
 * Copyright (c) 2024 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <mysqld_error.h>

using std::string;

namespace
{
const int normal_port = 4006;
const int skip_auth_port = 4007;
const int nomatch_port = 4008;
const int caseless_port = 4009;

void test_match_host_false(TestConnections& test);
void test_lower_case_table_names(TestConnections& test);
void expect_access_denied(TestConnections& test, int port, const string& user, const string& pass,
                          const string& db);
void expect_login_success(TestConnections& test, int port, const string& user, const string& pass,
                          const string& db);

bool try_normal_login(TestConnections& test, int port, const string& user, const string& pass,
                      const string& db = "");

void test_main(TestConnections& test)
{
    test.maxscale->check_print_servers_status({mxt::ServerInfo::master_st, mxt::ServerInfo::slave_st});
    if (test.ok())
    {
        test.repl->connect();
        test_match_host_false(test);
        test_lower_case_table_names(test);
    }
}

void test_match_host_false(TestConnections& test)
{
    const char create_fmt[] = "CREATE OR REPLACE USER '%s'@'%s' IDENTIFIED BY '%s';";
    test.tprintf("Create a user which can only connect from MaxScale IP. Should work with the listener with "
                 "authenticator_options=match_host=false.");
    string user = "maxhost_user";
    auto& mxs = *test.maxscale;
    auto userz = user.c_str();
    auto hostz = mxs.ip4();
    string pass = "maxhost_pass";
    MYSQL* conn = test.repl->nodes[0];
    test.try_query(conn, create_fmt, userz, hostz, pass.c_str());

    if (test.ok())
    {
        expect_access_denied(test, normal_port, user, pass, "");
        expect_access_denied(test, skip_auth_port, user, pass, "");

        test.tprintf("Testing listener with match_host=false.");
        bool login_success = try_normal_login(test, nomatch_port, user, pass);
        test.expect(login_success, "Login to port %i failed.", normal_port);
        if (test.ok())
        {
            test.tprintf("match_host=false works.");
        }
    }
    test.try_query(conn, "DROP USER '%s'@'%s';", userz, hostz);
}

void test_lower_case_table_names(TestConnections& test)
{
    // Test lower_case_table_names. Only test the MaxScale-side of authentication, as testing
    // the server is not really the purpose here.
    test.tprintf("Preparing to test lower_case_table_names.");
    const char create_fmt[] = "CREATE OR REPLACE USER '%s'@'%s' IDENTIFIED BY '%s';";
    string user = "low_case_user";
    string pass = "low_case_pass";
    auto userz = user.c_str();
    const char host[] = "%";
    MYSQL* conn = test.repl->nodes[0];
    test.try_query(conn, create_fmt, userz, host, pass.c_str());

    const char create_db_fmt[] = "CREATE OR REPLACE DATABASE %s;";
    const char grant_sel_fmt[] = "GRANT select on %s.* TO '%s'@'%s';";

    const char test_db1[] = "test_db1";
    test.try_query(conn, create_db_fmt, test_db1);
    test.try_query(conn, grant_sel_fmt, test_db1, userz, host);

    const char test_db2[] = "tEsT_db2";
    test.try_query(conn, create_db_fmt, test_db2);
    test.try_query(conn, grant_sel_fmt, test_db2, userz, host);

    const string login_db1 = "TeSt_dB1";
    const string login_db2 = "tESt_Db2";

    if (test.ok())
    {
        // Should not work, as requested db is not equal to real db.
        expect_access_denied(test, normal_port, user, pass, login_db1);
        expect_access_denied(test, normal_port, user, pass, login_db2);

        test.tprintf("Testing listener with lower_case_table_names=1");
        // Should work, as the login db is converted to lower case.
        expect_login_success(test, nomatch_port, user, pass, login_db1);
        // Should work even if target db is not lower case.
        expect_login_success(test, nomatch_port, user, pass, login_db2);
        if (test.ok())
        {
            test.tprintf("lower_case_table_names=1 works.");
        }

        test.tprintf("Testing listener with lower_case_table_names=2");
        // Should work, as listener compares db names case-insensitive.
        expect_login_success(test, caseless_port, user, pass, login_db2);
        if (test.ok())
        {
            test.tprintf("lower_case_table_names=2 works.");
        }

        // Check that log_password_mismatch works.
        expect_access_denied(test, caseless_port, user, "wrong_pass", "");
        test.log_includes("Client gave wrong password. Got hash");
        if (test.ok())
        {
            test.tprintf("log_password_mismatch works.");
        }
    }

    test.try_query(conn, "DROP USER '%s'@'%s';", user.c_str(), host);
    test.try_query(conn, "DROP DATABASE %s;", test_db1);
    test.try_query(conn, "DROP DATABASE %s;", test_db2);
}

void expect_access_denied(TestConnections& test, int port, const string& user, const string& pass,
                          const string& db)
{
    auto host = test.maxscale->ip4();
    MYSQL* maxconn = db.empty() ? open_conn_no_db(port, host, user, pass) :
        open_conn_db(port, host, db, user, pass);
    int ret = mysql_errno(maxconn);
    if ((db.empty() && ret == ER_ACCESS_DENIED_ERROR) || (!db.empty() && ret == ER_BAD_DB_ERROR))
    {
        test.tprintf("Login failed as expected: '%s'", mysql_error(maxconn));
    }
    else if (ret == 0)
    {
        test.add_failure("Login to db '%s' succeeded when failure was expected.", db.c_str());
    }
    else
    {
        test.add_failure("Unexpected error %i: '%s'", ret, mysql_error(maxconn));
    }
    mysql_close(maxconn);
}

void expect_login_success(TestConnections& test, int port, const string& user, const string& pass,
                          const string& db)
{
    auto host = test.maxscale->ip4();
    MYSQL* conn = open_conn_db(port, host, db, user, pass);
    int ret = mysql_errno(conn);
    test.expect(ret == 0, "Login to MaxScale port %i failed. Error %i: '%s'", port, ret, mysql_error(conn));
    mysql_close(conn);
}

bool try_normal_login(TestConnections& test, int port, const string& user, const string& pass,
                      const string& db)
{
    bool rval = false;
    auto host = test.maxscale->ip4();
    MYSQL* maxconn = db.empty() ? open_conn_no_db(port, host, user, pass) :
        open_conn_db(port, host, db, user, pass);

    auto err = mysql_error(maxconn);
    if (*err)
    {
        test.tprintf("Could not log in: '%s'", err);
    }
    else
    {
        if (execute_query_silent(maxconn, "SELECT rand();") == 0)
        {
            rval = true;
            test.tprintf("Logged in and queried successfully.");
        }
        else
        {
            test.tprintf("Query rejected: '%s'", mysql_error(maxconn));
        }
    }
    mysql_close(maxconn);
    return rval;
}
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
