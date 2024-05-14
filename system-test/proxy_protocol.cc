/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

/**
 * @file proxy_protocol.cpp proxy protocol test
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <string>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <maxbase/format.hh>
#include <maxbase/proxy_protocol.hh>
#include <maxbase/string.hh>

using std::string;
using SslMode = mxt::MariaDBServer::SslMode;

namespace
{
void test_main(TestConnections& test);
void check_conn(TestConnections& test, mxt::MariaDB* conn, bool expect_conn_success,
                const string& expected_ip);
void check_port(TestConnections& test, mxt::MariaDB* conn, int expected_port);
void addr_helper(TestConnections& test, int family, const string& addr_str, int port, sockaddr_storage& out);
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
                        testcon = be->try_open_connection(SslMode::OFF, proxy_user, proxy_pw);
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

        if (test.ok())
        {
            // Test MXS-3003: inbound proxy protocol
            enum class ProxyMode {TEXT, BIN};
            auto prepare_conn = [&test](const string& user, const string& pw, SslMode ssl,
                                        ProxyMode mode, const string& proxy_ip, int proxy_port) {
                auto conn = std::make_unique<mxt::MariaDB>(test.logger());
                auto& sett = conn->connection_settings();
                sett.user = user;
                sett.password = pw;
                if (ssl == SslMode::ON)
                {
                    auto base_dir = mxt::SOURCE_DIR;
                    sett.ssl.key = mxb::string_printf("%s/ssl-cert/client.key", base_dir);
                    sett.ssl.cert = mxb::string_printf("%s/ssl-cert/client.crt", base_dir);
                    sett.ssl.ca = mxb::string_printf("%s/ssl-cert/ca.crt", base_dir);
                    sett.ssl.enabled = true;
                }

                if (proxy_ip.empty())
                {
                    if (mode == ProxyMode::TEXT)
                    {
                        conn->set_local_text_proxy_header();
                    }
                    else
                    {
                        conn->set_local_bin_proxy_header();
                    }
                }
                else
                {
                    std::vector<uint8_t> header_bytes;
                    if (mode == ProxyMode::TEXT)
                    {
                        const char* protocol = (proxy_ip.find(':') == string::npos) ? "TCP4" : "TCP6";
                        string header = mxb::string_printf("PROXY %s %s %s %i %i\r\n", protocol,
                                                           proxy_ip.c_str(), test.maxscale->ip(), proxy_port,
                                                           test.maxscale->rwsplit_port);
                        auto ptr = reinterpret_cast<const uint8_t*>(header.data());
                        header_bytes.assign(ptr, ptr + header.size());
                    }
                    else
                    {
                        int family = (proxy_ip.find('/') != string::npos) ? AF_UNIX :
                            (proxy_ip.find(':') != string::npos) ? AF_INET6 : AF_INET;
                        sockaddr_storage peer_addr;
                        addr_helper(test, family, proxy_ip, proxy_port, peer_addr);
                        sockaddr_storage server_addr;   // ignored
                        auto header = mxb::proxy_protocol::gen_binary_header(peer_addr, server_addr);
                        header_bytes.assign(header.header, header.header + header.len);
                    }
                    conn->set_custom_proxy_header(std::move(header_bytes));
                }

                return conn;
            };

            auto update_users = [&mxs]() {
                mxs.try_open_rwsplit_connection("non-existing-user", "aabbcc");
                mxs.wait_for_monitor();
            };

            const string anyhost_un = "anyhost_user";
            const string anyhost_pw = "anyhost_pw";
            test.tprintf("Creating user '%s'", anyhost_un.c_str());
            repl.ping_or_open_admin_connections();
            auto adminconn = repl.backend(0)->admin_connection();
            auto anyhost_scopeuser = adminconn->create_user(anyhost_un, "%", anyhost_pw);
            update_users();

            string mxs_ip4 = mxs.ip4();
            int rwsplit_no_proxy_port = 4006;

            if (test.ok())
            {
                int rwsplit_all_proxy_port = 4007;
                int fake_port = 1234;

                test.tprintf("Check that the user works. Server should see client's real ip.");
                // MaxScale is sending proxy header to server regardless, so server sees real client ip.
                auto conn = mxs.try_open_connection(rwsplit_no_proxy_port, anyhost_un, anyhost_pw, "");
                check_conn(test, conn.get(), true, client_ip);

                test.tprintf("Check that sending a proxy header to a listener not configured for it fails.");
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::OFF, ProxyMode::TEXT,
                                    client_ip, fake_port);
                conn->try_open(mxs_ip4, rwsplit_no_proxy_port, "");
                check_conn(test, conn.get(), false, "");

                test.tprintf("Check that normal connection to a proxy enabled listener works.");
                conn = mxs.try_open_connection(rwsplit_all_proxy_port, anyhost_un, anyhost_pw, "");
                check_conn(test, conn.get(), true, client_ip);

                test.tprintf("Check that proxy connection to a proxy enabled listener works.");
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::OFF, ProxyMode::TEXT,
                                    client_ip, fake_port);
                conn->open(mxs_ip4, rwsplit_all_proxy_port, "");
                check_conn(test, conn.get(), true, client_ip);

                test.tprintf("Check that proxy connection from another ip to a proxy enabled "
                             "listener works.");
                const string fake_client_ip = "111.222.192.251";
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::OFF, ProxyMode::TEXT,
                                    fake_client_ip, fake_port);
                conn->open(mxs_ip4, rwsplit_all_proxy_port, "");
                check_conn(test, conn.get(), true, fake_client_ip);
                // Check that server sees the fake port.
                check_port(test, conn.get(), fake_port);

                test.tprintf("Same as above, with a binary header.");
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::OFF, ProxyMode::BIN,
                                    fake_client_ip, fake_port);
                conn->open(mxs_ip4, rwsplit_all_proxy_port, "");
                check_conn(test, conn.get(), true, fake_client_ip);
                check_port(test, conn.get(), fake_port);

                test.tprintf("Test empty proxy header (local connection).");
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::OFF, ProxyMode::TEXT, "", 0);
                conn->open(mxs_ip4, rwsplit_all_proxy_port, "");
                check_conn(test, conn.get(), true, client_ip);

                test.tprintf("Test empty binary proxy header (local connection).");
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::OFF, ProxyMode::BIN, "", 0);
                conn->open(mxs_ip4, rwsplit_all_proxy_port, "");
                check_conn(test, conn.get(), true, client_ip);
            }

            if (test.ok())
            {
                // Repeat previous tests with ssl.
                int fake_port = 1337;
                int ssl_proxy_port = 4008;

                test.tprintf("Check that sending a proxy header + SSL to a listener not configured for it "
                             "fails.");
                auto conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::ON, ProxyMode::TEXT,
                                         client_ip, fake_port);
                conn->try_open(mxs_ip4, rwsplit_no_proxy_port, "");
                check_conn(test, conn.get(), false, "");

                test.tprintf("Check that normal SSL connection to a proxy enabled listener works.");
                conn = mxs.try_open_connection(mxt::MaxScale::SslMode::ON, ssl_proxy_port,
                                               anyhost_un, anyhost_pw, "");
                check_conn(test, conn.get(), true, client_ip);

                test.tprintf("Check that SSL proxy connection to a proxy enabled listener works.");
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::ON, ProxyMode::TEXT,
                                    client_ip, fake_port);
                conn->open(mxs_ip4, ssl_proxy_port, "");
                check_conn(test, conn.get(), true, client_ip);

                test.tprintf("Check that SSL proxy connection from another ip to a proxy enabled "
                             "listener works.");
                const string fake_client_ip = "121.202.191.222";
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::ON, ProxyMode::TEXT,
                                    fake_client_ip, fake_port);
                conn->open(mxs_ip4, ssl_proxy_port, "");
                check_conn(test, conn.get(), true, fake_client_ip);
                check_port(test, conn.get(), fake_port);

                test.tprintf("Same as above, with a binary header.");
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::ON, ProxyMode::BIN,
                                    fake_client_ip, fake_port);
                conn->open(mxs_ip4, ssl_proxy_port, "");
                check_conn(test, conn.get(), true, fake_client_ip);
                check_port(test, conn.get(), fake_port);

                test.tprintf("Test empty proxy header with SSL.");
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::ON, ProxyMode::TEXT, "", 0);
                conn->open(mxs_ip4, ssl_proxy_port, "");
                check_conn(test, conn.get(), true, client_ip);

                test.tprintf("Test empty binary proxy header with SSL.");
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::ON, ProxyMode::BIN, "", 0);
                conn->open(mxs_ip4, ssl_proxy_port, "");
                check_conn(test, conn.get(), true, client_ip);
            }

            if (test.ok())
            {
                auto set_proxy_nws = [&test](const string& value) {
                    string alter_cmd = mxb::string_printf("alter listener RWS-Listener-proxy-multi "
                                                          "proxy_protocol_networks %s", value.c_str());
                    auto res = test.maxscale->maxctrl(alter_cmd);
                    test.expect(res.rc == 0 && res.output == "OK", "Alter command '%s' failed.",
                                alter_cmd.c_str());
                };

                int alter_listener_port = 4009;
                const string fake_client_ip = "123.101.202.123";
                int fake_port = 1111;
                test.tprintf("Check that sending a proxy header to a listener not configured for it fails.");
                auto conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::OFF, ProxyMode::TEXT,
                                         fake_client_ip, fake_port);
                conn->try_open(mxs_ip4, alter_listener_port, "");
                check_conn(test, conn.get(), false, "");

                test.tprintf("Check that listener works after configuring proxy networks.");
                set_proxy_nws(client_ip);
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::OFF, ProxyMode::TEXT,
                                    fake_client_ip, fake_port);
                conn->try_open(mxs_ip4, alter_listener_port, "");
                check_conn(test, conn.get(), true, fake_client_ip);
                check_port(test, conn.get(), fake_port);

                test.tprintf("Check that listener works after configuring proxy networks to ipv6.");
                string new_proxy_nws = "::ffff:" + client_ip;
                set_proxy_nws(new_proxy_nws);
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::OFF, ProxyMode::TEXT,
                                    fake_client_ip, fake_port);
                conn->try_open(mxs_ip4, alter_listener_port, "");
                check_conn(test, conn.get(), true, fake_client_ip);
                check_port(test, conn.get(), fake_port);

                test.tprintf("Configure proxy network to imaginary ip. Check that proxy header is denied but "
                             "normal login works.");
                new_proxy_nws = fake_client_ip;
                set_proxy_nws(new_proxy_nws);
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::OFF, ProxyMode::TEXT,
                                    fake_client_ip, fake_port);
                conn->try_open(mxs_ip4, alter_listener_port, "");
                check_conn(test, conn.get(), false, "");

                conn = mxs.try_open_connection(mxt::MaxScale::SslMode::OFF, alter_listener_port,
                                               anyhost_un, anyhost_pw, "");
                check_conn(test, conn.get(), true, client_ip);

                test.tprintf("Configuring proxy networks with mask...");
                string altered_client_ip = client_ip;
                auto dot_pos = altered_client_ip.find('.');
                altered_client_ip.resize(dot_pos);
                altered_client_ip.append(".111.222.111/8");
                set_proxy_nws(altered_client_ip);
                test.tprintf("Proxy networks configured to '%s'", altered_client_ip.c_str());
                test.tprintf("Checking that logging in with proxy header works.");
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::OFF, ProxyMode::TEXT,
                                    fake_client_ip, fake_port);
                conn->try_open(mxs_ip4, alter_listener_port, "");
                check_conn(test, conn.get(), true, fake_client_ip);
                check_port(test, conn.get(), fake_port);

                test.tprintf("Same as above, with a binary header.");
                conn = prepare_conn(anyhost_un, anyhost_pw, SslMode::OFF, ProxyMode::BIN,
                                    fake_client_ip, fake_port);
                conn->try_open(mxs_ip4, alter_listener_port, "");
                check_conn(test, conn.get(), true, fake_client_ip);
                check_port(test, conn.get(), fake_port);
            }
        }

        // Restore server settings.
        test.tprintf("Removing proxy setting from server1.");
        be->stop_database();
        repl.restore_server_settings(0);
        be->start_database();
    }
}

void check_conn(TestConnections& test, mxt::MariaDB* conn, bool expect_conn_success,
                const string& expected_ip)
{
    bool conn_ok = false;
    bool query_ok = false;
    if (conn->is_open())
    {
        conn_ok = true;
        const string q = "select user();";
        auto userhost = conn->simple_query(q);
        if (!userhost.empty())
        {
            query_ok = true;
            if (auto [_, ip] = mxb::split(userhost, "@"); !ip.empty())
            {
                if (!expected_ip.empty())
                {
                    if (ip == expected_ip)
                    {
                        test.tprintf("Server sees host '%s', as expected.", expected_ip.c_str());
                    }
                    else
                    {
                        test.add_failure("Wrong result from '%s'. Expected '%s', got '%s'.",
                                         q.c_str(), expected_ip.c_str(), ip.data());
                    }
                }
            }
            else
            {
                test.add_failure("Malformed result from '%s': '%s'", q.c_str(), userhost.c_str());
            }
        }
    }
    if (expect_conn_success)
    {
        test.expect(conn_ok && query_ok, "Connection and/or query failed when it should have "
                                         "succeeded.");
    }
    else
    {
        test.expect(!conn_ok, "Connection succeeded when it should have failed.");
    }
}

void check_port(TestConnections& test, mxt::MariaDB* conn, int expected_port)
{
    string host_query = "select host from information_schema.processlist "
                        "WHERE ID = connection_id()";
    auto host_str = conn->simple_query(host_query);
    if (!host_str.empty())
    {
        if (auto [_, port_str] = mxb::split(host_str, ":"); !port_str.empty())
        {
            int found_port = atoi(port_str.data());
            if (found_port == expected_port)
            {
                test.tprintf("Server sees port %i, as expected.", expected_port);
            }
            else
            {
                test.add_failure("Server sees port %i when %i was expected.",
                                 found_port, expected_port);
            }
        }
        else
        {
            test.add_failure("Unexpected host query result: '%s'", host_str.c_str());
        }
    }
    else
    {
        test.add_failure("Query '%s' failed or returned nothing.", host_query.c_str());
    }
}

void addr_helper(TestConnections& test, int family, const string& addr_str, int port, sockaddr_storage& out)
{
    if (family == AF_INET)
    {
        auto* dst = (sockaddr_in*)&out;
        auto* dst_addr = &dst->sin_addr;
        if (inet_pton(family, addr_str.c_str(), dst_addr) == 1)
        {
            dst->sin_family = family;
            dst->sin_port = htons(port);
        }
        else
        {
            test.add_failure("inet_pton() failed for '%s'", addr_str.c_str());
        }
    }
    else if (family == AF_INET6)
    {
        auto* dst = (sockaddr_in6*)&out;
        auto* dst_addr = &dst->sin6_addr;
        if (inet_pton(family, addr_str.c_str(), dst_addr) == 1)
        {
            dst->sin6_family = family;
            dst->sin6_port = htons(port);
        }
        else
        {
            test.add_failure("inet_pton() failed for '%s'", addr_str.c_str());
        }
    }
    else if (family == AF_UNIX)
    {
        auto* dst = (sockaddr_un*)&out;
        strcpy(dst->sun_path, addr_str.c_str());
        dst->sun_family = family;
    }
}
}
