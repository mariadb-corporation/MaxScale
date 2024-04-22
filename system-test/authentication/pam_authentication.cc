/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <iostream>
#include <string>
#include <maxbase/format.hh>
#include <maxtest/mariadb_connector.hh>
#include <maxtest/execute_cmd.hh>

using std::string;
using std::cout;

namespace
{
const int N = 2;    // Use just two backends so that setup is fast.
const string plugin_path = string(mxt::BUILD_DIR) + "/../connector-c/install/lib/mariadb/plugin";
const char install_plugin[] = "INSTALL SONAME 'auth_pam';";
const char uninstall_plugin[] = "UNINSTALL SONAME 'auth_pam';";
const char create_pam_user_fmt[] = "CREATE OR REPLACE USER '%s'@'%%' IDENTIFIED VIA pam USING '%s';";
const char pam_user[] = "dduck";
const char pam_pw[] = "313";
const char pam_config_name[] = "pam_config_msg";

MYSQL* pam_login(TestConnections& test, int port, const string& user, const string& pass,
                 const string& database);
bool test_pam_login(TestConnections& test, int port, const string& user, const string& pass,
                    const string& database);
bool try_mapped_pam_login(TestConnections& test, int port, const string& user, const string& pass,
                          const string& expected_user);

void test_main(TestConnections& test);
void test_pam_cleartext_plugin(TestConnections& test);
void test_user_account_mapping(TestConnections& test);
}

int main(int argc, char** argv)
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

namespace
{
void test_main(TestConnections& test)
{
    test.expect(test.repl->N >= N, "Test requires at least two backends.");
    if (!test.ok())
    {
        return;
    }

    const string read_shadow = "chmod o+r /etc/shadow";
    const string read_shadow_off = "chmod o-r /etc/shadow";
    const string pam_message_contents = "Lorem ipsum";

    // To make most out of this test, use a custom pam service configuration. It needs to be written to
    // all backends.

    string pam_config_path_src = mxb::string_printf("%s/authentication/%s", mxt::SOURCE_DIR, pam_config_name);
    string pam_config_path_dst = mxb::string_printf("/etc/pam.d/%s", pam_config_name);

    const char pam_msgfile[] = "pam_test_msg.txt";
    string pam_msgfile_path_src = mxb::string_printf("%s/authentication/%s", mxt::SOURCE_DIR, pam_msgfile);
    string pam_msgfile_path_dst = mxb::string_printf("/tmp/%s", pam_msgfile);

    const string delete_pam_conf_cmd = "rm -f " + pam_config_path_dst;
    const string delete_pam_message_cmd = "rm -f " + pam_msgfile_path_dst;

    test.repl->connect();

    // Prepare the backends for PAM authentication. Enable the plugin and create a user. Also,
    // make /etc/shadow readable for all so that the server process can access it.

    for (int i = 0; i < N; i++)
    {
        MYSQL* conn = test.repl->nodes[i];
        test.try_query(conn, install_plugin);

        auto& vm = test.repl->backend(i)->vm_node();
        vm.add_linux_user(pam_user, pam_pw);
        vm.run_cmd_sudo(read_shadow);

        // Also, copy the custom pam config and message file.
        vm.copy_to_node_sudo(pam_config_path_src, pam_config_path_dst);
        vm.copy_to_node_sudo(pam_msgfile_path_src, pam_msgfile_path_dst);
    }

    // Also create the user on the node running MaxScale, as the MaxScale PAM plugin compares against
    // local users.
    auto& mxs = *test.maxscale;
    auto& mxs_vm = mxs.vm_node();
    mxs_vm.add_linux_user(pam_user, pam_pw);
    mxs_vm.run_cmd_sudo(read_shadow);
    mxs_vm.copy_to_node_sudo(pam_config_path_src, pam_config_path_dst);
    mxs_vm.copy_to_node_sudo(pam_msgfile_path_src, pam_msgfile_path_dst);

    if (test.ok())
    {
        cout << "PAM-plugin installed and users created on all servers. Starting MaxScale.\n";
        mxs.restart();
        mxs.check_print_servers_status({mxt::ServerInfo::master_st, mxt::ServerInfo::slave_st});
    }
    else
    {
        cout << "Test preparations failed.\n";
    }

    // Helper function for checking PAM-login. If db is empty, log to null database.
    auto try_log_in = [&test](const string& user, const string& pass, const string& database) {
            int port = test.maxscale->rwsplit_port;
            if (!test_pam_login(test, port, user, pass, database))
            {
                test.expect(false, "PAM login failed.");
            }
        };

    auto update_users = [&mxs]() {
            mxs.stop();
            mxs.delete_log();
            mxs.start();
            mxs.wait_for_monitor();
        };

    if (test.ok())
    {
        // First, test that MaxCtrl login with the pam user works.
        string cmd = mxb::string_printf("-u %s -p %s show maxscale", pam_user, pam_pw);
        test.check_maxctrl(cmd);
        if (test.ok())
        {
            cout << "'maxctrl " << cmd << "' works.\n";
        }

        // MXS-4355: Token authentication does not work with PAM users
        auto res = test.maxctrl(mxb::string_printf("-u %s -p %s api get auth meta.token", pam_user, pam_pw));
        test.expect(res.rc == 0, "'maxctrl api get' failed: %s", res.output.c_str());

        auto token = res.output.substr(1, res.output.size() - 2);
        int rc = test.maxscale->ssh_node_f(
            false, "curl -f -s -H 'Authorization: Bearer %s' localhost:8989/v1/maxscale", token.c_str());
        test.expect(rc == 0, "Token authentication with PAM user failed.");
        test.tprintf("Token authentication with PAM: %s", rc == 0 ? "OK" : "Failed");
    }

    if (test.ok())
    {
        auto& repl = test.repl;
        auto conn = repl->backend(0)->open_connection();
        // Create a PAM user + a normal user.
        auto pam_usr = conn->create_user(pam_user, "%", pam_config_name, "pam");
        pam_usr.grant("SELECT ON *.*");

        const char basic_un[] = "basic";
        const char basic_pw[] = "basic_pw";
        auto basic_user = conn->create_user(basic_un, "%", basic_pw);

        repl->sync_slaves();
        update_users();
        mxs.get_servers().print();

        test.tprintf("Testing normal PAM user.");
        try_log_in(pam_user, pam_pw, "");
        test.log_includes(pam_message_contents.c_str());

        if (test.ok())
        {
            // MXS-4731, com_change_user between different authenticators.
            test.tprintf("Testing COM_CHANGE_USER from native user to pam user.");
            auto basic_conn = mxs.try_open_rwsplit_connection(basic_un, basic_pw);
            // This bypasses MXS-4758. Remove when/if that issue is ever fixed.
            auto res = basic_conn->query("select rand();");
            test.expect(res && res->next_row(), "Query before COM_CHANGE_USER failed.");
            auto changed = basic_conn->change_user(pam_user, pam_pw, "test");
            test.expect(changed, "COM_CHANGE_USER %s->%s failed.", basic_un, pam_user);
            if (changed)
            {
                res = basic_conn->query("select rand();");
                test.expect(res && res->next_row(), "Query after COM_CHANGE_USER failed.");
            }
        }
    }

    if (test.ok())
    {
        const char dummy_user[] = "proxy-target";
        const char dummy_pw[] = "unused_pw";
        // Basic PAM authentication seems to be working. Now try with an anonymous user proxying to
        // the real user. The following does not actually do proper user mapping, as that requires further
        // setup on the backends. It does however demonstrate that MaxScale detects the anonymous user and
        // accepts the login of a non-existent user with PAM.
        test.tprintf("Creating anonymous catch-all user and proxy target user.");
        auto conn = test.repl->backend(0)->admin_connection();
        conn->cmd_f(create_pam_user_fmt, "", pam_config_name);
        conn->cmd_f("CREATE OR REPLACE USER '%s'@'%%' IDENTIFIED BY '%s';", dummy_user, dummy_pw);
        test.tprintf("Grant proxy privs to anonymous user.");
        conn->cmd_f("GRANT PROXY ON '%s'@'%%' TO ''@'%%';", dummy_user);

        test.repl->sync_slaves();
        update_users();
        mxs.get_servers().print();

        if (test.ok())
        {
            // Again, try logging in with the same user.
            cout << "Testing anonymous proxy user.\n";
            try_log_in(pam_user, pam_pw, "");
            test.log_includes(pam_message_contents.c_str());
        }

        // Remove the created users.
        conn->cmd_f("DROP USER '%s'@'%%';", dummy_user);
        conn->cmd_f("DROP USER ''@'%%';");
    }

    if (test.ok())
    {
        // Test roles. Create a user without privileges but with a default role. The role has another role
        // which finally has the privileges to the db.
        MYSQL* conn = test.repl->nodes[0];
        test.try_query(conn, create_pam_user_fmt, pam_user, pam_config_name);
        const char create_role_fmt[] = "CREATE ROLE %s;";
        const char grant_role_fmt[] = "GRANT %s TO %s;";
        const char r1[] = "role1";
        const char r2[] = "role2";
        const char r3[] = "role3";
        const char dbname[] = "empty_db";

        // pam_user->role1->role2->role3->privilege
        test.try_query(conn, "CREATE OR REPLACE DATABASE %s;", dbname);
        test.try_query(conn, create_role_fmt, r1);
        test.try_query(conn, create_role_fmt, r2);
        test.try_query(conn, create_role_fmt, r3);
        test.try_query(conn, "GRANT %s TO '%s'@'%%';", r1, pam_user);
        test.try_query(conn, "SET DEFAULT ROLE %s for '%s'@'%%';", r1, pam_user);
        test.try_query(conn, grant_role_fmt, r2, r1);
        test.try_query(conn, grant_role_fmt, r3, r2);
        test.try_query(conn, "GRANT SELECT ON *.* TO '%s';", r3);
        test.try_query(conn, "FLUSH PRIVILEGES;");
        test.repl->sync_slaves();
        update_users();

        // If ok so far, try logging in with PAM.
        if (test.ok())
        {
            cout << "Testing normal PAM user with role-based privileges.\n";
            try_log_in(pam_user, pam_pw, dbname);
            test.log_includes(pam_message_contents.c_str());
        }

        // Remove the created items.
        test.try_query(conn, "DROP USER '%s'@'%%';", pam_user);
        test.try_query(conn, "DROP DATABASE %s;", dbname);
        const char drop_role_fmt[] = "DROP ROLE %s;";
        test.try_query(conn, drop_role_fmt, r1);
        test.try_query(conn, drop_role_fmt, r2);
        test.try_query(conn, drop_role_fmt, r3);
    }

    if (test.ok())
    {
        // Test that normal authentication on the same port works. This tests MXS-2497.
        auto maxconn = test.maxscale->open_rwsplit_connection();
        int port = test.maxscale->rwsplit_port;
        test.try_query(maxconn, "SELECT rand();");
        cout << "Normal mariadb-authentication on port " << port << (test.ok() ? " works.\n" : " failed.\n");
        mysql_close(maxconn);
    }

    // Remove the linux user from the MaxScale node. Required for next test cases.
    mxs_vm.remove_linux_user(pam_user);

    int normal_port = test.maxscale->rwsplit_port;
    int skip_auth_port = 4007;

    const char login_failed_msg[] = "Login to port %i failed.";
    if (test.ok())
    {
        cout << "\n";
        // Recreate the pam user.
        MYSQL* conn = test.repl->nodes[0];
        test.try_query(conn, create_pam_user_fmt, pam_user, pam_config_name);
        // Normal listener should not work anymore, but the one with skip_authentication should work
        // even with the Linux user removed.

        bool login_success = test_pam_login(test, normal_port, pam_user, pam_pw, "");
        test.expect(!login_success, "Normal login succeeded when it should not have.");

        cout << "Testing listener with skip_authentication.\n";
        login_success = test_pam_login(test, skip_auth_port, pam_user, pam_pw, "");
        test.expect(login_success, login_failed_msg, skip_auth_port);
        if (test.ok())
        {
            cout << "skip_authentication works.\n";
        }
        test.try_query(conn, "DROP USER '%s'@'%%';", pam_user);
    }

    if (test.ok())
    {
        test_pam_cleartext_plugin(test);
    }

    if (test.ok())
    {
        test_user_account_mapping(test);
    }

    test.tprintf("Test complete. Cleaning up.");
    // Cleanup: remove linux user and files from the MaxScale node.
    mxs_vm.remove_linux_user(pam_user);
    mxs_vm.run_cmd_sudo(read_shadow_off);
    mxs_vm.run_cmd_sudo(delete_pam_conf_cmd);
    mxs_vm.run_cmd_sudo(delete_pam_message_cmd);

    // Cleanup: remove the linux users on the backends, unload pam plugin.
    for (int i = 0; i < N; i++)
    {
        MYSQL* conn = test.repl->nodes[i];
        test.try_query(conn, uninstall_plugin);
        auto& vm = test.repl->backend(i)->vm_node();
        vm.remove_linux_user(pam_user);
        vm.run_cmd_sudo(read_shadow_off);
        vm.run_cmd_sudo(delete_pam_conf_cmd);
        vm.run_cmd_sudo(delete_pam_message_cmd);
    }

    test.repl->disconnect();
}

void test_pam_cleartext_plugin(TestConnections& test)
{
    int cleartext_port = 4010;
    const string setting_name = "pam_use_cleartext_plugin";
    const string setting_val = setting_name + "=1";
    auto& mxs_vm = test.maxscale->vm_node();
    auto& repl = *test.repl;

    auto check_cleartext_val = [&](int node, bool expected) {
        auto conn = repl.backend(node)->admin_connection();
        auto res = conn->simple_query("select @@pam_use_cleartext_plugin;");
        string expected_str = expected ? "1" : "0";
        test.expect(res == expected_str, "Wrong value of %s. Got %s, expected %s.",
                    setting_name.c_str(), res.c_str(), expected_str.c_str());
    };
    auto alter_cleartext_setting = [&](int node, bool enable) {
        repl.stop_node(node);
        if (enable)
        {
            repl.stash_server_settings(node);
            repl.add_server_setting(node, setting_val.c_str());
        }
        else
        {
            repl.restore_server_settings(node);
        }
        repl.start_node(node);
        repl.connect(node);
    };

    cout << "Enabling " << setting_name << " on all backends.\n";
    for (int i = 0; i < N; i++)
    {
        check_cleartext_val(i, false);
        alter_cleartext_setting(i, true);
        check_cleartext_val(i, true);
    }

    if (test.ok())
    {
        // The user needs to be recreated on the MaxScale node.
        mxs_vm.add_linux_user(pam_user, pam_pw);
        // Using the standard password service 'passwd' is unreliable, as it can change between
        // distributions. Copy a minimal pam config and use it.
        const char pam_min_cfg[] = "pam_config_simple";
        string pam_min_cfg_src = mxb::string_printf("%s/authentication/%s", mxt::SOURCE_DIR, pam_min_cfg);
        string pam_min_cfg_dst = mxb::string_printf("/etc/pam.d/%s", pam_min_cfg);
        mxs_vm.copy_to_node_sudo(pam_min_cfg_src, pam_min_cfg_dst);
        // Copy to VMs.
        for (int i = 0; i < N; i++)
        {
            repl.backend(i)->vm_node().copy_to_node_sudo(pam_min_cfg_src, pam_min_cfg_dst);
        }

        test.tprintf("Testing listener with '%s'.", setting_val.c_str());
        MYSQL* conn = repl.nodes[0];
        test.try_query(conn, create_pam_user_fmt, pam_user, pam_min_cfg);
        // Try to log in with wrong pw to ensure user data is updated.
        bool login_success = test_pam_login(test, cleartext_port, "wrong", "wrong", "");
        test.expect(!login_success, "Login succeeded when it should not have.");
        login_success = test_pam_login(test, cleartext_port, pam_user, pam_pw, "");
        if (login_success)
        {
            test.tprintf("'%s' works.", setting_name.c_str());
        }
        else
        {
            test.add_failure("Login with %s failed", setting_name.c_str());
        }
        test.try_query(conn, "DROP USER '%s'@'%%';", pam_user);

        mxs_vm.delete_from_node(pam_min_cfg_dst);
        for (int i = 0; i < N; i++)
        {
            repl.backend(i)->vm_node().delete_from_node(pam_min_cfg_dst);
        }
    }

    cout << "Disabling " << setting_name << " on all backends.\n";
    for (int i = 0; i < N; i++)
    {
        alter_cleartext_setting(i, false);
        check_cleartext_val(i, false);
    }
}

void test_user_account_mapping(TestConnections& test)
{
    // Test user account mapping (MXS-3475). For this, the pam_user_map.so-file is required.
    // This file is installed with the server, but not with MaxScale. Depending on distro, the file
    // may be in different places. Check both.
    // Copy the pam mapping module to the MaxScale VM. Also copy pam service config and mapping config.
    int user_map_port = 4011;
    auto& mxs_vm = test.maxscale->vm_node();
    pam::copy_user_map_lib(test.repl->backend(0)->vm_node(), mxs_vm);
    pam::copy_map_config(mxs_vm);

    const char pam_map_config_name[] = "pam_config_user_map";

    if (test.ok())
    {
        // For this case, it's enough to create the Linux user on the MaxScale VM.
        const char orig_user[] = "orig_pam_user";
        const char orig_pass[] = "orig_pam_pw";
        const char mapped_user[] = "mapped_mariadb";
        const char mapped_pass[] = "mapped_pw";

        mxs_vm.add_linux_user(orig_user, orig_pass);
        // Due to recent changes, the mapped user must exist as well.
        mxs_vm.add_linux_user(mapped_user, mapped_pass);

        auto srv = test.repl->backend(0);
        auto conn = srv->try_open_connection();
        string create_orig_user_query = mxb::string_printf(create_pam_user_fmt,
                                                           orig_user, pam_map_config_name);
        conn->cmd(create_orig_user_query);

        string create_mapped_user_query = mxb::string_printf("create or replace user '%s'@'%%';",
                                                             mapped_user);
        conn->cmd(create_mapped_user_query);
        // Try to login with wrong username so MaxScale updates accounts.
        sleep(1);
        bool login_success = test_pam_login(test, user_map_port, "wrong", "wrong", "");
        test.expect(!login_success, "Login succeeded when it should not have.");
        sleep(1);
        bool mapped_login_ok = try_mapped_pam_login(test, user_map_port, orig_user, orig_pass,
                                                    mapped_user);
        test.expect(mapped_login_ok, "Mapped login failed.");

        // Cleanup
        const char drop_user_fmt[] = "DROP USER '%s'@'%%';";
        string drop_orig_user_query = mxb::string_printf(drop_user_fmt, orig_user);
        conn->cmd(drop_orig_user_query);
        string drop_mapped_user_query = mxb::string_printf(drop_user_fmt, mapped_user);
        conn->cmd(drop_mapped_user_query);
        mxs_vm.remove_linux_user(orig_user);
        mxs_vm.remove_linux_user(mapped_user);
    }

    // Delete config files from MaxScale VM.
    pam::delete_map_config(mxs_vm);
    // Delete the library file from both the tester VM and MaxScale VM.
    pam::delete_user_map_lib(mxs_vm);
}

// Helper function for checking PAM-login. If db is empty, log to null database.
MYSQL* pam_login(TestConnections& test, int port, const string& user, const string& pass,
                 const string& database)
{
    const char* host = test.maxscale->ip4();
    const char* db = nullptr;
    if (!database.empty())
    {
        db = database.c_str();
    }

    if (db)
    {
        printf("Trying to log in to [%s]:%i as %s with database %s.\n", host, port, user.c_str(), db);
    }
    else
    {
        printf("Trying to log in to [%s]:%i as %s.\n", host, port, user.c_str());
    }

    MYSQL* rval = nullptr;
    MYSQL* maxconn = mysql_init(NULL);
    // Need to set plugin directory so that dialog.so is found.
    mysql_optionsv(maxconn, MYSQL_PLUGIN_DIR, plugin_path.c_str());
    mysql_real_connect(maxconn, host, user.c_str(), pass.c_str(), db, port, NULL, 0);
    auto err = mysql_error(maxconn);
    if (*err)
    {
        test.tprintf("Could not log in: '%s'", err);
        mysql_close(maxconn);
    }
    else
    {
        rval = maxconn;
    }
    return rval;
}

bool test_pam_login(TestConnections& test, int port, const string& user, const string& pass,
                    const string& database)
{
    bool rval = false;
    auto maxconn = pam_login(test, port, user, pass, database);
    if (maxconn)
    {
        if (execute_query_silent(maxconn, "SELECT rand();") == 0)
        {
            rval = true;
            cout << "Logged in and queried successfully.\n";
        }
        else
        {
            cout << "Query rejected: '" << mysql_error(maxconn) << "'\n";
        }
    }
    return rval;
}

bool try_mapped_pam_login(TestConnections& test, int port, const string& user, const string& pass,
                          const string& expected_user)
{
    bool rval = false;
    auto maxconn = pam_login(test, port, user, pass, "");
    if (maxconn)
    {
        auto res = get_result(maxconn, "select user();");
        if (!res.empty())
        {
            string effective_user = res[0][0];
            effective_user = mxt::cutoff_string(effective_user, '@');
            if (effective_user == expected_user)
            {
                test.tprintf("Logged in. Mapped user is '%s', as expected.", effective_user.c_str());
                rval = true;
            }
            else
            {
                test.tprintf("User '%s' mapped to '%s' when '%s' was expected.",
                             user.c_str(), effective_user.c_str(), expected_user.c_str());
            }
        }
        else
        {
            cout << "Query rejected: '" << mysql_error(maxconn) << "'\n";
        }
    }
    return rval;
}
}
