/*
 * Copyright (c) 2023 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <string>
#include <maxbase/format.hh>

using std::string;

namespace
{
void test_main(TestConnections& test);
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    // At this point, MaxScale cannot connect to the server since it's not expecting a proxy header.
    mxs.check_print_servers_status({mxt::ServerInfo::DOWN});

    if (test.ok())
    {
        auto& repl = *test.repl;
        const char* mxs_ip = mxs.ip4();
        const int mxs_port = mxs.rwsplit_port;

        // Activate proxy protocol on server1. Enough to test on just one backend.
        test.tprintf("Setting up proxy protocol on server1.");
        auto be = repl.backend(0);
        be->stop_database();
        repl.stash_server_settings(0);

        string proxy_setting = mxb::string_printf("proxy_protocol_networks=%s", mxs_ip);
        repl.add_server_setting(0, proxy_setting.c_str());
        repl.add_server_setting(0, "skip-name-resolve=1");      // To disable server hostname resolution.
        be->start_database();
        test.tprintf("Proxy protocol set up.");
        mxs.sleep_and_wait_for_monitor(1, 2);   // Wait for server to start and be detected

        mxs.check_print_servers_status({mxt::ServerInfo::master_st});

        string client_ip;
        // Send the user query directly to backend to get its view.
        auto be_conn = be->open_connection();
        string client_userhost = be_conn->simple_query("SELECT USER();");
        if (!client_userhost.empty())
        {
            auto at_pos = client_userhost.find('@');
            if (at_pos != string::npos && client_userhost.length() > at_pos + 1)
            {
                client_ip = client_userhost.substr(at_pos + 1, string::npos);
                test.tprintf("Client IP is %s", client_ip.c_str());
                test.tprintf("MaxScale IP is %s and port is %i", mxs_ip, mxs_port);
                test.tprintf("Server IP is %s", repl.ip4(0));
            }
        }

        test.expect(!client_ip.empty(), "Could not read client ip.");

        const string proxy_user = "proxy_user";
        const string proxy_pw = "proxy_pwd";
        if (test.ok())
        {
            auto adminconn = mxs.open_rwsplit_connection2();
            test.expect(adminconn->is_open(), "MaxScale connection failed.");
            if (adminconn->is_open())
            {
                // Remove any existing conflicting usernames. Usually these should not exist.
                test.tprintf("Removing any leftover users.");
                adminconn->cmd_f("DROP USER IF EXISTS '%s'@'%%'", proxy_user.c_str());
                adminconn->cmd_f("DROP USER IF EXISTS '%s'@'%s'", proxy_user.c_str(), mxs_ip);
                adminconn->cmd_f("DROP USER IF EXISTS '%s'@'%s'", proxy_user.c_str(), client_ip.c_str());

                mxs.try_open_rwsplit_connection("qwerty", "asdf");      // Forces users reload.

                // Try to connect through MaxScale using the proxy-user, it shouldn't work yet.
                auto testcon = mxs.try_open_connection(mxs_port, proxy_user, proxy_pw);
                test.expect(!testcon->is_open(), "Connection to MaxScale succeeded when it should have "
                                                 "failed.");

                if (test.ok())
                {
                    // Create a test table and the proxy user.
                    test.tprintf("Creating user '%s'", proxy_user.c_str());
                    adminconn->cmd("CREATE OR REPLACE TABLE test.t1(id INT)");
                    adminconn->cmd_f("CREATE USER '%s'@'%s' identified by '%s'",
                                     proxy_user.c_str(), client_ip.c_str(), proxy_pw.c_str());
                    adminconn->cmd_f("GRANT SELECT,INSERT ON test.t1 TO '%s'@'%s'",
                                     proxy_user.c_str(), client_ip.c_str());
                    if (test.ok())
                    {
                        test.tprintf("User created.");
                        // Test the user account by connecting directly to the server, it should work.
                        testcon = be->try_open_connection(mxt::MariaDBServer::SslMode::OFF,
                                                          proxy_user, proxy_pw);
                        test.expect(testcon->is_open(), "Connection to server1 as %s failed when success "
                                                        "was expected.", proxy_user.c_str());

                        // The test user should be able to log in also through MaxScale.
                        testcon = mxs.try_open_rwsplit_connection(proxy_user, proxy_pw);
                        test.expect(testcon->is_open(), "Connection to MaxScale as %s failed when success "
                                                        "was expected.", proxy_user.c_str());
                        if (testcon->is_open())
                        {
                            // Try some queries to ensure it's working.
                            testcon->cmd("INSERT INTO test.t1 VALUES (232);");
                            testcon->cmd("INSERT INTO test.t1 VALUES (232);");
                            int expected_rows = 2;
                            auto query_res = testcon->query("SELECT * FROM test.t1;");
                            if (query_res)
                            {
                                auto found_rows = query_res->get_row_count();
                                test.expect(found_rows == expected_rows, "Unexpected query results.");
                            }
                        }
                    }

                    test.tprintf("Removing test user and table.");
                    adminconn = mxs.open_rwsplit_connection2();
                    adminconn->cmd("DROP TABLE IF EXISTS test.t1");
                    adminconn->cmd_f("DROP USER IF EXISTS '%s'@'%s'", proxy_user.c_str(), client_ip.c_str());
                }
            }
        }

        /**
         * MXS-2252: Proxy Protocol not displaying originating IP address in SHOW PROCESSLIST
         * https://jira.mariadb.org/browse/MXS-2252
         */
        Connection direct = test.repl->get_connection(0);
        Connection rwsplit = test.maxscale->rwsplit();
        direct.connect();
        rwsplit.connect();
        auto d = direct.field("SELECT USER()");
        auto r = rwsplit.field("SELECT USER()");
        test.tprintf("Direct: %s Readwritesplit: %s", d.c_str(), r.c_str());
        test.expect(d == r, "Both connections should return the same user: %s != %s", d.c_str(), r.c_str());

        // Restore server settings.
        test.tprintf("Removing proxy setting from server1.");
        be->stop_database();
        repl.restore_server_settings(0);
        be->start_database();
    }
}
}
