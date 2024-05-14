/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file bug448.cpp bug448 regression case ("Wildcard in host column of mysql.user table don't work properly")
 *
 * Test creates user1@xxx.%.%.% and tries to use it to connect
 */

#include <maxbase/format.hh>
#include <maxtest/get_my_ip.hh>
#include <maxtest/testconnections.hh>

using std::string;

void mxs5048_hex_prefix_wildcard(TestConnections& test, const char* my_ip)
{
    test.tprintf("Add '%s beefburger' to /etc/hosts", my_ip);
    test.maxscale->ssh_node_f(true, "echo '%s beefburger' >> /etc/hosts", my_ip);

    std::string mxs_ip = test.maxscale->ip4();
    auto c = test.maxscale->rwsplit();
    c.connect();

    // Create a user for the test that on the MaxScale server requires a hostname wildcard to match but on the
    // MariaDB server it will match an exact IP address.
    c.query("CREATE USER 'bob'@'beef%' IDENTIFIED BY 'bob'");
    c.query("GRANT ALL ON *.* TO 'bob'@'beef%'");
    c.query("CREATE USER 'bob'@'" + mxs_ip + "' IDENTIFIED BY 'bob'");
    c.query("GRANT ALL ON *.* TO 'bob'@'" + mxs_ip + "'");
    test.repl->sync_slaves();

    // The user should be allowed access through MaxScale
    auto b = test.maxscale->rwsplit();
    b.set_credentials("bob", "bob");
    test.expect(b.connect(), "Connection should work: %s", b.error());
    test.expect(b.query("SELECT 1"), "Query should work: %s", b.error());

    c.query("DROP USER 'bob'@'beef%'");
    c.query("DROP USER 'bob'@'" + mxs_ip + "'");

    test.maxscale->ssh_node_f(true, "sed -i '/beefburger/ d' /etc/hosts");
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    auto mxs = test.maxscale;
    test.repl->connect();

    char my_ip[1024];
    std::string original_ip;

    if (get_my_ip(mxs->ip4(), my_ip) == 0)
    {
        original_ip = my_ip;
        test.tprintf("Test machine IP (got via network request) %s\n", my_ip);
        string my_real_ip = my_ip;
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

        auto admin_conn = mxs->conn_rwsplit;
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
                test.maxscale->restart();
                mxs->wait_for_monitor();

                test.tprintf("Testing database grant with wildcard...");
                test_login(grant_db, true);
                test_login(fail_db1, true);
                test_login(fail_db2, true);
            }

            test.maxscale->connect();
            admin_conn = mxs->conn_rwsplit;
            const char drop_db_fmt[] = "drop database %s;";
            const char drop_db_failed[] = "DROP DATABASE failed";
            test.add_result(execute_query(admin_conn, drop_db_fmt, grant_db), drop_db_failed);
            test.add_result(execute_query(admin_conn, drop_db_fmt, fail_db1), drop_db_failed);
            test.add_result(execute_query(admin_conn, drop_db_fmt, fail_db2), drop_db_failed);
        }

        if (test.ok())
        {
            // MXS-1827 Test a more complicated netmask.
            // Hardly a good test, just here to have a netmask that is not just 255 or 0.
            string userhost2_str = mxb::string_printf("'netmask'@'%s/%s'", my_real_ip.c_str(),
                                                      my_real_ip.c_str());
            auto userhost2 = userhost2_str.c_str();
            test.tprintf("Testing host pattern with netmask by logging in to user account %s.", userhost2);
            test.add_result(execute_query(admin_conn, "CREATE USER %s identified by '%s';", userhost2, pw),
                            "Failed to create user");
            test.check_maxctrl("reload service RW-Split-Router");
            auto conn = mxs->try_open_rwsplit_connection("netmask", pw, "");
            test.expect(conn->is_open(), "Connection failed: %s", conn->error());
            test.add_result(execute_query(admin_conn, "DROP USER %s;", userhost2), "Failed to delete user");
        }

        test.add_result(execute_query(admin_conn, "DROP USER %s;", userhostc), "Drop user failed");
    }
    else
    {
        test.add_failure("get_my_ip() failed");
    }

    // MXS-5048: Wildcard host with hex prefix is mistakenly treated as an IPv6 address
    if (test.ok())
    {
        mxs5048_hex_prefix_wildcard(test, original_ip.c_str());
    }

    test.check_maxscale_alive();
    test.repl->disconnect();
    return test.global_result;
}
