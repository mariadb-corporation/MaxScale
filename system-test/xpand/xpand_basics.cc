/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

#include <string>
#include <maxtest/testconnections.hh>
#include <maxtest/maxrest.hh>

using std::string;

namespace
{
const std::string monitor_name = "Xpand-Monitor";
auto master = mxt::ServerInfo::master_st;
auto down = mxt::ServerInfo::DOWN;
auto base_states = {master, master, master, master, master};

void check_for_servers(TestConnections& test)
{
    const string bootstrap_server = "bootstrap_server";

    auto servers = test.maxscale->get_servers();
    servers.print();

    test.expect(servers.size() == 4 + 1, "Expected 5 servers (1 bootstrap + 4 discovered.");
    servers.check_servers_status(base_states);

    bool bootstrap_found = false;
    string prefix = "@@" + monitor_name;

    for (const auto& server : servers)
    {
        const string& name = server.name;
        if (name == bootstrap_server)
        {
            bootstrap_found = true;
        }
        else
        {
            test.expect(name.find(prefix) == 0, "The name of a dynamic Xpand node (%s) does not "
                                                "start with \"%s\".", name.c_str(), prefix.c_str());
        }
    }
    test.expect(bootstrap_found, "Did not find server '%s'.", bootstrap_server.c_str());
}

void check_state_change(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& xpand = *test.xpand;

    // The Xpand-monitor depends on the internal monitor of the Xpand-cluster itself. Since it has a delay,
    // some sleeps are required when expecting state changes.
    const int cycles = 4;

    mxs.check_print_servers_status(base_states);

    int node = 0;
    string address = xpand.ip_private(node);

    test.tprintf("Blocking node %i and waiting for %i monitor ticks.", node, cycles);

    xpand.block_node(node);
    mxs.sleep_and_wait_for_monitor(cycles, cycles);
    mxs.check_print_servers_status({down, down, master, master, master});

    test.tprintf("Unblocking node %i and waiting for %i monitor ticks.", node, cycles);
    xpand.unblock_node(node);
    mxs.sleep_and_wait_for_monitor(cycles, cycles);
    mxs.check_print_servers_status(base_states);
}

void check_softfailing(TestConnections& test)
{
    int node = 4;

    auto expect_node_status = [&test, node](mxt::ServerInfo::bitfield expected) {
        auto servers = test.maxscale->get_servers();
        servers.print();
        auto server = servers.get(node);
        test.expect(server.status == expected, "Wrong status. Found %s, expected %s.",
                    server.status_to_string().c_str(), mxt::ServerInfo::status_to_string(expected).c_str());
    };
    expect_node_status(master);

    auto server_info = test.maxscale->get_servers().get(node);
    auto srvname = server_info.name;

    try
    {
        MaxRest maxrest(&test);
        test.tprintf("Softfailing %s.", srvname.c_str());
        maxrest.call_command("xpandmon", "softfail", monitor_name, {srvname});
        expect_node_status(master | mxt::ServerInfo::DRAINED);

        test.tprintf("Unsoftfailing %s.", srvname.c_str());
        maxrest.call_command("xpandmon", "unsoftfail", monitor_name, {srvname});
        expect_node_status(master);
    }
    catch (const std::exception& x)
    {
        test.add_failure("Exception: %s", x.what());
    }
}

void check_login(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& xpand = *test.xpand;

    const char drop_fmt[] = "DROP USER %s;";
    const char create_fmt[] = "CREATE USER %s IDENTIFIED BY '%s';";
    const char grant_fmt[] = "GRANT SELECT ON test.* TO %s;";
    const char super_user[] = "super_user";
    const char super_user_host[] = "'super_user'@'%'";
    const char super_pw[] = "super_pw";

    const char db_user[] = "db_user";
    const char db_user_host[] = "'db_user'@'%'";
    const char db_pw[] = "db_pw";

    const char no_db_user[] = "no_db_acc_user";
    const char no_db_user_host[] = "'no_db_acc_user'@'%'";
    const char no_db_pw[] = "no_db_acc_pw";

    test.tprintf("Testing logging in. Stopping MaxScale and creating users.");
    mxs.stop();

    auto conn = xpand.backend(0)->open_connection();
    conn->try_cmd_f(drop_fmt, super_user_host);
    conn->try_cmd_f(drop_fmt, db_user_host);
    conn->try_cmd_f(drop_fmt, no_db_user_host);

    conn->cmd_f(create_fmt, super_user_host, super_pw);
    conn->cmd_f("GRANT SUPER ON *.* TO %s;", super_user_host);
    conn->cmd_f(grant_fmt, super_user_host);

    conn->cmd_f(create_fmt, db_user_host, db_pw);
    conn->cmd_f(grant_fmt, db_user_host);
    conn->cmd_f(create_fmt, no_db_user_host, no_db_pw);

    sleep(1);
    test.tprintf("Users created, starting MaxScale.");
    mxs.start();
    sleep(1);

    auto servers_info = mxs.get_servers();
    if (mxs.ssl())
    {
        // Test is in ssl-mode. Check that backends accept ssl-connections.
        for (int i = 0; i < xpand.N; i++)
        {
            auto be = xpand.backend(i);
            auto ssl_conn = be->try_open_connection(mxt::MariaDBServer::SslMode::ON, "");
            if (ssl_conn->is_open())
            {
                test.tprintf("SSL connection to backend %i works.", i);
            }
            else
            {
                test.add_failure("SSL connection to backend %i failed.", i);
            }
        }

        // Xpand does not support "require ssl"-mode for users, so just logging in does not prove that
        // MaxScale enforces ssl. Check rest-api for ssl settings.
        for (const auto& srv_info : servers_info)
        {
            test.expect(srv_info.ssl_configured, "SSL is not configured on backend %s.",
                        srv_info.name.c_str());
        }
    }
    else
    {
        for (const auto& srv_info : servers_info)
        {
            test.expect(!srv_info.ssl_configured, "SSL is configured on backend %s when it should not be.",
                        srv_info.name.c_str());
        }
    }

    auto test_login = [&test](int port, const string& user, const string& pw, const string& db,
                              bool expect_success) {
        test.tprintf("Logging in to db '%s' as user '%s'.", db.c_str(), user.c_str());
        auto c = test.maxscale->try_open_connection(port, user, pw, db);
        if (expect_success)
        {
            test.expect(c->is_open(), "Connection failed: '%s'", c->error());
            if (c->is_open())
            {
                auto res = c->query("select rand();");
                test.expect(res.get(), "Query failed.");
                if (res)
                {
                    test.tprintf("Login and query success.");
                }
            }
        }
        else
        {
            test.expect(!c->is_open(), "Connection succeeded when failure was expected.");
        }
    };

    int port = mxs.rwsplit_port;
    if (test.ok())
    {
        test.tprintf("Testing normal rwsplit service.");
        test_login(port, super_user, super_pw, "", true);
        test_login(port, super_user, super_pw, "test", true);
        test_login(port, db_user, db_pw, "test", true);
    }

    if (test.ok())
    {
        // Login works but query will fail. Login will start failing if Xpand-user management is improved
        // at some point.
        test.tprintf("Logging in to db 'test' as user '%s'.", no_db_user);
        auto test_conn = mxs.try_open_connection(port, no_db_user, no_db_pw, "test");
        test.expect(test_conn->is_open(), "Connection failed.");
        if (test_conn->is_open())
        {
            auto res = test_conn->try_query("select rand();");
            test.expect(!res, "Query succeeded when failure was expected.");
        }
    }

    conn->cmd_f(drop_fmt, super_user_host);
    conn->cmd_f(drop_fmt, db_user_host);
    conn->cmd_f(drop_fmt, no_db_user_host);
}

void test_main(TestConnections& test)
{
    check_for_servers(test);
    check_login(test);
    check_state_change(test);
    check_softfailing(test);
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
