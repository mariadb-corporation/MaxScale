/**
 * @file bug448.cpp bug448 regression case ("Wildcard in host column of mysql.user table don't work properly")
 *
 * Test creates user1@xxx.%.%.% and tries to use it to connect
 */

#include <maxbase/format.hh>
#include <maxtest/get_my_ip.hh>
#include <maxtest/testconnections.hh>

using std::string;

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    auto mxs = test.maxscales;
    test.set_timeout(20);
    test.repl->connect();

    char my_ip[1024];
    if (get_my_ip(mxs->ip4(), my_ip) == 0)
    {
        test.tprintf("Test machine IP (got via network request) %s\n", my_ip);

        char* first_dot = strstr(my_ip, ".");
        strcpy(first_dot, ".%.%.%");
        test.tprintf("Test machine IP with %% %s\n", my_ip);

        const char un[] = "user1";
        const char pw[] = "pass1";
        string userhost = mxb::string_printf("%s@'%s'", un, my_ip);
        auto userhostc = userhost.c_str();

        test.tprintf("Connecting to Maxscale\n");
        test.add_result(mxs->connect_maxscale(), "Error connecting to Maxscale\n");
        test.tprintf("Creating user %s", userhostc);
        test.set_timeout(30);

        auto admin_conn = mxs->conn_rwsplit[0];
        test.add_result(execute_query(admin_conn, "CREATE USER %s identified by '%s';", userhostc, pw),
                        "Failed to create user");

        auto query = mxb::string_printf("GRANT ALL PRIVILEGES ON *.* TO %s;", userhostc);
        test.add_result(execute_query(admin_conn, "%s", query.c_str()), "GRANT failed");

        auto test_login = [&](const string& db, bool expect_success) {
                auto conn = open_conn_db(mxs->rwsplit_port, mxs->ip4(), db, un, pw, test.maxscale_ssl);

                bool success = (mysql_errno(conn) == 0);
                const char* success_str = success ? "succeeded" : "failed";
                if (success == expect_success)
                {
                    test.tprintf("Authentification for %s to database %s %s, as expected",
                                 userhostc, db.c_str(), success_str);
                }
                else
                {
                    auto errmsg = success ? "none" :  mysql_error(conn);
                    test.add_failure(
                        "Authentification for %s to database %s %s, against expectation. Error: %s",
                        userhostc, db.c_str(), success_str, errmsg);
                }
                mysql_close(conn);
            };

        if (test.ok())
        {
            test.tprintf("Trying to open connection using %s", un);
            test_login("test", true);
        }

        query = mxb::string_printf("REVOKE ALL PRIVILEGES ON *.* FROM %s; FLUSH PRIVILEGES;", userhostc);
        test.add_result(execute_query(admin_conn, "%s", query.c_str()), "REVOKE failed");

        // MXS-3172 Test logging on to database when grant includes a wildcard.
        if (test.ok())
        {
            const char grant_db[] = "Area5_Files";
            const char fail_db1[] = "Area51Files";
            const char fail_db2[] = "Area52Files";

            const char create_db_fmt[] = "create database %s;";
            const char create_db_failed[] = "CREATE DATABASE failed";
            test.add_result(execute_query(admin_conn, create_db_fmt, grant_db), create_db_failed);
            test.add_result(execute_query(admin_conn, create_db_fmt, fail_db1), create_db_failed);
            test.add_result(execute_query(admin_conn, create_db_fmt, fail_db2), create_db_failed);

            const char grant_fmt[] = "GRANT SELECT ON `%s`.* TO %s;";
            const char revoke_fmt[] = "REVOKE SELECT ON `%s`.* FROM %s;";
            const char escaped_wc_db[] = "Area5\\_Files";
            query = mxb::string_printf(grant_fmt, escaped_wc_db, userhostc);
            test.add_result(execute_query(admin_conn, "%s", query.c_str()), "GRANT failed.");

            if (test.ok())
            {
                test.tprintf("Testing database grant with escaped wildcard...");
                test_login(grant_db, true);
                test_login(fail_db1, false);
                test_login(fail_db2, false);
            }

            // Replace escaped wc grant with non-escaped version.
            query = mxb::string_printf(revoke_fmt, escaped_wc_db, userhostc);
            test.add_result(execute_query(admin_conn, "%s", query.c_str()), "REVOKE failed.");

            const char wc_db[] = "Area5_Files";
            query = mxb::string_printf(grant_fmt, wc_db, userhostc);
            test.add_result(execute_query(admin_conn, "%s", query.c_str()), "GRANT failed.");

            if (test.ok())
            {
                // Restart MaxScale to reload users, as the load limit may have been reached.
                test.maxscales->restart();
                mxs->wait_for_monitor();

                test.tprintf("Testing database grant with wildcard...");
                test_login(grant_db, true);
                test_login(fail_db1, true);
                test_login(fail_db2, true);
            }

            test.maxscales->connect();
            admin_conn = mxs->conn_rwsplit[0];
            const char drop_db_fmt[] = "drop database %s;";
            const char drop_db_failed[] = "DROP DATABASE failed";
            test.add_result(execute_query(admin_conn, drop_db_fmt, grant_db), drop_db_failed);
            test.add_result(execute_query(admin_conn, drop_db_fmt, fail_db1), drop_db_failed);
            test.add_result(execute_query(admin_conn, drop_db_fmt, fail_db2), drop_db_failed);
        }

        test.add_result(execute_query(admin_conn, "DROP USER %s;", userhostc), "Drop user failed");
    }
    else
    {
        test.add_failure("get_my_ip() failed");
    }

    test.check_maxscale_alive(0);
    test.repl->disconnect();
    return test.global_result;
}
