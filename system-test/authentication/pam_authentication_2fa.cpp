/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxbase/format.hh>
#include <maxtest/execute_cmd.hh>
#include <maxtest/mariadb_connector.hh>
#include <maxtest/testconnections.hh>
#include "../mariadbmonitor/fail_switch_rejoin_common.cpp"

using std::string;
using std::cout;
using namespace std::literals::string_literals;
using mxb::string_printf;
using mxt::ServerInfo;

bool test_pam_login(TestConnections& test, int port, const string& user, const string& pass,
                    const string& pass2);

string generate_2fa_token(TestConnections& test, const string& secret);

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    test.repl->connect();

    const char install_plugin[] = "INSTALL SONAME 'auth_pam';";
    const char uninstall_plugin[] = "UNINSTALL SONAME 'auth_pam';";
    // Only works on Centos.
    const char install_google_auth[] = "yum -y install google-authenticator";

    const char pam_user[] = "twofactor_user";
    const char pam_pw[] = "twofactor_pass";
    const char pam_config_name[] = "twofactor_conf";

    // The authenticator secret file needs to be owned by the process doing the authentication (either
    // "maxscale" or "mysql". Also, the user to change to needs to be set in pam config.
    const char maxscale_user[] = "maxscale";
    const char mysql_user[] = "mysql";

    const string add_user_cmd = "useradd "s + pam_user;
    const string add_pw_cmd = mxb::string_printf("printf \"%s:%s\" | chpasswd", pam_user, pam_pw);
    const string read_shadow = "chmod o+r /etc/shadow";

    const string remove_user_cmd = "userdel --remove "s + pam_user;
    const string read_shadow_off = "chmod o-r /etc/shadow";

    const string pam_config_file_path = "/etc/pam.d/"s + pam_config_name;

    // Use a somewhat non-standard pam config. Does not affect the validity of the test, as we are not
    // testing the security of the google authenticator itself.
    const char pam_config_contents_fmt[] = R"(
auth            required        pam_unix.so
auth            required        pam_google_authenticator.so nullok user=%s allowed_perm=0777 secret=/tmp/.google_authenticator
account         required        pam_unix.so
)";

    const string pam_config_mxs_contents = string_printf(pam_config_contents_fmt, maxscale_user);
    const string pam_config_srv_contents = string_printf(pam_config_contents_fmt, mysql_user);

    const string gauth_secret_key = "3C7OP37ONKJOELVIMNZ67AADSY";
    const string gauth_keyfile_contents = gauth_secret_key + "\n" +
R"(\" RATE_LIMIT 3 30
\" TOTP_AUTH
74865607
49583434
76566817
48621211
71963974)";
    const char gauth_secret_path[] = "/tmp/.google_authenticator";

    const char write_file_fmt[] = "printf \"%s\" > %s";
    const string create_pam_conf_mxs_cmd = string_printf(write_file_fmt, pam_config_mxs_contents.c_str(),
                                                         pam_config_file_path.c_str());
    const string create_pam_conf_srv_cmd = string_printf(write_file_fmt, pam_config_srv_contents.c_str(),
                                                         pam_config_file_path.c_str());
    const string delete_pam_conf_cmd = "rm -f " + pam_config_file_path;

    const string create_2fa_secret_cmd = string_printf(write_file_fmt,
                                                       gauth_keyfile_contents.c_str(), gauth_secret_path);
    const string chown_2fa_secret_mxs_cmd = string_printf("chown %s %s", maxscale_user, gauth_secret_path);
    const string chown_2fa_secret_srv_cmd = string_printf("chown %s %s", mysql_user, gauth_secret_path);
    const string delete_2fa_secret_cmd = (string)"rm -f " + gauth_secret_path;

    const int N = 2;
    auto cleanup = [&]() {
            // Cleanup: remove linux user and files from the MaxScale node.
            test.maxscales->ssh_node_f(0, true, "%s", remove_user_cmd.c_str());
            test.maxscales->ssh_node_f(0, true, "%s", read_shadow_off.c_str());
            test.maxscales->ssh_node_f(0, true, "%s", delete_pam_conf_cmd.c_str());
            test.maxscales->ssh_node_f(0, true, "%s", delete_2fa_secret_cmd.c_str());

            // Cleanup: remove the linux users on the backends, unload pam plugin.
            for (int i = 0; i < N; i++)
            {
                MYSQL* conn = test.repl->nodes[i];
                execute_query(conn, "%s", uninstall_plugin);
                test.repl->ssh_node_f(i, true, "%s", remove_user_cmd.c_str());
                test.repl->ssh_node_f(i, true, "%s", read_shadow_off.c_str());
                test.repl->ssh_node_f(i, true, "%s", delete_pam_conf_cmd.c_str());
                test.repl->ssh_node_f(i, true, "%s", delete_2fa_secret_cmd.c_str());
            }
        };

    auto initialize = [&]() {
            // Setup pam 2fa on the MaxScale node + on two MariaDB-nodes. The configs are slightly different
            // on the two machine types.
            for (int i = 0; i < N; i++)
            {
                MYSQL* conn = test.repl->nodes[i];
                test.try_query(conn, "%s", install_plugin);
                test.repl->ssh_node_f(i, true, "%s", install_google_auth);
                test.repl->ssh_node_f(i, true, "%s", add_user_cmd.c_str());
                test.repl->ssh_node_f(i, true, "%s", add_pw_cmd.c_str());
                test.repl->ssh_node_f(i, true, "%s", read_shadow.c_str());
                test.repl->ssh_node_f(i, true, "%s", create_pam_conf_srv_cmd.c_str());
                test.repl->ssh_node_f(i, true, "%s", create_2fa_secret_cmd.c_str());
                test.repl->ssh_node_f(i, true, "%s", chown_2fa_secret_srv_cmd.c_str());
            }

            test.maxscales->ssh_node_f(0, true, "%s", install_google_auth);
            // Create the user on the node running MaxScale, as the MaxScale PAM plugin compares against
            // local users.
            test.maxscales->ssh_node_f(0, true, "%s", add_user_cmd.c_str());
            test.maxscales->ssh_node_f(0, true, "%s", add_pw_cmd.c_str());
            test.maxscales->ssh_node_f(0, true, "%s", read_shadow.c_str());
            test.maxscales->ssh_node_f(0, true, "%s", create_pam_conf_mxs_cmd.c_str());
            test.maxscales->ssh_node_f(0, true, "%s", create_2fa_secret_cmd.c_str());
            test.maxscales->ssh_node_f(0, true, "%s", chown_2fa_secret_mxs_cmd.c_str());
        };

    cleanup();      // remove conflicting usernames and files, just in case.
    initialize();

    if (test.ok())
    {
        test.tprintf("PAM-plugin installed and users created on all servers.");
        auto& mxs = test.maxscale();
        auto expected_states = {ServerInfo::master_st, ServerInfo::slave_st};
        mxs.check_servers_status(expected_states);

        if (test.ok())
        {
            const char create_pam_user_fmt[] = "CREATE OR REPLACE USER '%s'@'%%' "
                                               "IDENTIFIED VIA pam USING '%s';";
            auto create_user_query = mxb::string_printf(create_pam_user_fmt, pam_user, pam_config_name);
            auto admin_conn = mxs.open_rwsplit_connection();
            admin_conn->cmd(create_user_query);
            auto grant_query = mxb::string_printf("GRANT SELECT on test.* TO '%s'@'%%';", pam_user);
            admin_conn->cmd(grant_query);

            if (test.ok())
            {
                auto twofa_token = generate_2fa_token(test, gauth_secret_key);
                if (!twofa_token.empty())
                {
                    auto succ = test_pam_login(test, test.maxscales->port(), pam_user, pam_pw, twofa_token);
                    test.expect(succ, "Two-factor login failed");
                    if (test.ok())
                    {
                        test.tprintf("Try an invalid 2FA-code");
                        succ = test_pam_login(test, test.maxscales->port(), pam_user, pam_pw,
                                              twofa_token + "1");
                        test.expect(!succ, "Two-factor login succeeded when it should have failed");
                    }
                }
            }

            auto drop_user_query = mxb::string_printf("DROP USER '%s'@'%%';", pam_user);
            admin_conn->cmd(drop_user_query);
        }
    }
    else
    {
        cout << "Test preparations failed.\n";
    }

    cleanup();
    test.repl->disconnect();
}

// Helper function for checking PAM-login. If db is empty, log to null database.
bool test_pam_login(TestConnections& test, int port, const string& user, const string& pass,
                    const string& pass2)
{
    const char* host = test.maxscales->ip4();

    test.tprintf("Trying to log in to [%s]:%i as %s, with passwords '%s' and '%s'.\n",
                 host, port, user.c_str(), pass.c_str(), pass2.c_str());

    bool rval = false;

    // Using two passwords is a bit tricky as connector-c does not have a setting for it. Instead, invoke
    // a java app from the commandline.
    auto res = jdbc::test_connection(jdbc::ConnectorVersion::MARIADB_270, host, port, user, pass, pass2,
                                     "select '313';");
    if (res.success && res.output == "313\n")
    {
        rval = true;
        test.tprintf("Logged in and queried successfully.");
    }
    else
    {
        test.tprintf("Login or query failed");
    }
    return rval;
}

string generate_2fa_token(TestConnections& test, const string& secret)
{
    string rval;
    // Use oathtool to generate a time-limited password.
    auto cmd = mxb::string_printf("oathtool -b --totp %s", secret.c_str());
    auto process = popen(cmd.c_str(), "r");     // can only read from the pipe
    if (process)
    {
        int n = 100;
        char buf[n];
        memset(buf, 0, n);
        fgets(buf, n - 1, process);
        int rc = pclose(process);
        test.expect(rc == 0, "Command '%s' returned %i", cmd.c_str(), rc);
        // 2FA tokens are six numbers long.
        int output_len = strlen(buf);
        int token_len = 6;
        if (output_len == token_len + 1)
        {
            rval.assign(buf, buf + token_len);
        }
        else
        {
            test.add_failure("Failed to generate 2FA token. oathtool output: %s", buf);
        }
    }
    return rval;
}
