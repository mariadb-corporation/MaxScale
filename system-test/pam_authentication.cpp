/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include "mariadbmonitor/fail_switch_rejoin_common.cpp"
#include <iostream>
#include <string>

using std::string;
using std::cout;

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.repl->connect();
    delete_slave_binlogs(test);

    const char pam_user[] = "dduck";
    const char pam_pw[] = "313";
    const char pam_config_name[] = "duckburg";

    const string add_user_cmd = (string)"useradd " + pam_user;
    const string add_pw_cmd = (string)"echo " + pam_user + ":" + pam_pw + " | chpasswd";
    const string read_shadow = "chmod o+r /etc/shadow";

    const string remove_user_cmd = (string)"userdel --remove " + pam_user;
    const string read_shadow_off = "chmod o-r /etc/shadow";

    // To make most out of this test, use a custom pam service configuration. It needs to be written to
    // all backends.

    const string pam_config_file = (string)"/etc/pam.d/" + pam_config_name;
    const string pam_message_file = "/tmp/messages.txt";
    const string pam_config_contents =
            "auth             optional        pam_echo.so file=" + pam_message_file + "\n"
            "auth             required        pam_unix.so audit\n"
            "auth             optional        pam_echo.so file=" + pam_message_file + "\n"
            "auth             required        pam_unix.so audit\n"
            "account          required        pam_unix.so audit\n";

    const string pam_message_contents = "I know what you did last summer!";

    const string create_pam_conf_cmd = "printf \"" + pam_config_contents + "\" > " + pam_config_file;
    const string create_pam_message_cmd = "printf \"" + pam_message_contents + "\" > " + pam_message_file;
    const string delete_pam_conf_cmd = "rm -f " + pam_config_file;
    const string delete_pam_message_cmd = "rm -f " + pam_message_file;

    test.repl->connect();
    // Prepare the backends for PAM authentication. Enable the plugin and create a user. Also,
    // make /etc/shadow readable for all so that the server process can access it.
    for (int i = 0; i < test.repl->N; i++)
    {
        MYSQL* conn = test.repl->nodes[i];
        test.try_query(conn, "INSTALL SONAME 'auth_pam';");
        test.repl->ssh_node_f(i, true, "%s", add_user_cmd.c_str());
        test.repl->ssh_node_f(i, true, "%s", add_pw_cmd.c_str());
        test.repl->ssh_node_f(i, true, "%s", read_shadow.c_str());

        // Also, create the custom pam config and message file.
        test.repl->ssh_node_f(i, true, "%s", create_pam_conf_cmd.c_str());
        test.repl->ssh_node_f(i, true, "%s", create_pam_message_cmd.c_str());
    }

    // Also create the user on the node running MaxScale, as the MaxScale PAM plugin compares against
    // local users.
    test.maxscales->ssh_node_f(0, true, "%s", add_user_cmd.c_str());
    test.maxscales->ssh_node_f(0, true, "%s", add_pw_cmd.c_str());
    test.maxscales->ssh_node_f(0, true, "%s", read_shadow.c_str());
    test.maxscales->ssh_node_f(0, true, "%s", create_pam_conf_cmd.c_str());
    test.maxscales->ssh_node_f(0, true, "%s", create_pam_message_cmd.c_str());

    if (test.ok())
    {
        cout << "PAM-plugin installed and users created on all servers. Starting MaxScale.\n";
    }
    else
    {
        cout << "Test preparations failed.\n";
    }



    auto expect_server_status = [&test](const string& server_name, const string& status) {
            auto set_to_string = [](const StringSet& str_set) -> string {
                string rval;
                string sep;
                for (const string& elem : str_set)
                {
                    rval += elem + sep;
                    sep = ", ";
                }
                return rval;
            };

            auto status_set = test.maxscales->get_server_status(server_name.c_str());
            string status_str = set_to_string(status_set);
            bool found = (status_set.count(status) == 1);
            test.expect(found, "%s was not %s as was expected. Status: %s.",
                        server_name.c_str(), status.c_str(), status_str.c_str());
        };

    string server_names[] = {"server1", "server2", "server3", "server4"};
    string master = "Master";
    string slave = "Slave";

    if (test.ok())
    {
        get_output(test);
        print_gtids(test);

        expect_server_status(server_names[0], master);
        expect_server_status(server_names[1], slave);
        expect_server_status(server_names[2], slave);
        expect_server_status(server_names[3], slave);
    }

    // Helper function for checking PAM-login. If db is empty, log to null database.
    auto try_log_in = [&test](const string& user, const string& pass, const string& database) {
        const char* host = test.maxscales->IP[0];
        int port = test.maxscales->ports[0][0];
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

        MYSQL* maxconn = mysql_init(NULL);
        test.expect(maxconn, "mysql_init failed");
        if (maxconn)
        {
            // Need to set plugin directory so that dialog.so is found.
            const char plugin_path[] = "../connector-c/install/lib/mariadb/plugin";
            mysql_optionsv(maxconn, MYSQL_PLUGIN_DIR, plugin_path);
            mysql_real_connect(maxconn, host, user.c_str(), pass.c_str(), db, port, NULL, 0);
            auto err = mysql_error(maxconn);
            if (*err)
            {
                test.expect(false, "Could not log in: '%s'", err);
            }
            else
            {
                test.try_query(maxconn, "SELECT rand();");
                if (test.ok())
                {
                    cout << "Logged in and queried successfully.\n";
                }
                else
                {
                    cout << "Query rejected: '" << mysql_error(maxconn) << "'\n";
                }
            }
            mysql_close(maxconn);
        }
    };

    auto update_users = [&test]() {
        test.maxscales->execute_maxadmin_command(0, "reload dbusers RWSplit-Router");
    };

    const char create_pam_user_fmt[] = "CREATE OR REPLACE USER '%s'@'%%' IDENTIFIED VIA pam USING '%s';";
    if (test.ok())
    {
        MYSQL* conn = test.repl->nodes[0];
        // Create the PAM user on the master, it will replicate. Use the standard password service for
        // authenticating.
        test.try_query(conn, create_pam_user_fmt, pam_user, pam_config_name);
        test.try_query(conn, "GRANT SELECT ON *.* TO '%s'@'%%';", pam_user);
        test.try_query(conn, "FLUSH PRIVILEGES;");
        sleep(1);
        test.repl->sync_slaves();
        update_users();
        get_output(test);

        // If ok so far, try logging in with PAM.
        if (test.ok())
        {
            cout << "Testing normal PAM user.\n";
            try_log_in(pam_user, pam_pw, "");
            test.log_includes(0, pam_message_contents.c_str());
        }

        // Remove the created user.
        test.try_query(conn, "DROP USER '%s'@'%%';", pam_user);
    }

    if (test.ok())
    {
        const char dummy_user[] = "proxy-target";
        const char dummy_pw[] = "unused_pw";
        // Basic PAM authentication seems to be working. Now try with an anonymous user proxying to
        // the real user. The following does not actually do proper user mapping, as that requires further
        // setup on the backends. It does however demonstrate that MaxScale detects the anonymous user and
        // accepts the login of a non-existent user with PAM.
        MYSQL* conn = test.repl->nodes[0];
        // Add a user which will be proxied.
        test.try_query(conn, "CREATE OR REPLACE USER '%s'@'%%' IDENTIFIED BY '%s';", dummy_user, dummy_pw);

        // Create the anonymous catch-all user and allow it to proxy as the "proxy-target", meaning it
        // gets the target's privileges. Granting the proxy privilege is a bit tricky since only the local
        // root user can give it.
        test.try_query(conn, create_pam_user_fmt, "", pam_config_name);
        test.repl->ssh_node_f(0, true, "echo \"GRANT PROXY ON '%s'@'%%' TO ''@'%%'; FLUSH PRIVILEGES;\" | "
                                       "mysql --user=root",
                                       dummy_user);
        sleep(1);
        test.repl->sync_slaves();
        update_users();
        get_output(test);

        if (test.ok())
        {
            // Again, try logging in with the same user.
            cout << "Testing anonymous proxy user.\n";
            try_log_in(pam_user, pam_pw, "");
            test.log_includes(0, pam_message_contents.c_str());
        }

        // Remove the created users.
        test.try_query(conn, "DROP USER '%s'@'%%';", dummy_user);
        test.try_query(conn, "DROP USER ''@'%%';");
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
        sleep(1);
        test.repl->sync_slaves();
        update_users();
        get_output(test);

        // If ok so far, try logging in with PAM.
        if (test.ok())
        {
            cout << "Testing normal PAM user with role-based privileges.\n";
            try_log_in(pam_user, pam_pw, dbname);
            test.log_includes(0, pam_message_contents.c_str());
        }

        // Remove the created items.
        test.try_query(conn, "DROP USER '%s'@'%%';", pam_user);
        test.try_query(conn, "DROP DATABASE %s;", dbname);
        const char drop_role_fmt[] = "DROP ROLE %s;";
        test.try_query(conn, drop_role_fmt, r1);
        test.try_query(conn, drop_role_fmt, r2);
        test.try_query(conn, drop_role_fmt, r3);
    }

    // Cleanup: remove the linux users on the backends and MaxScale node, unload pam plugin.
    for (int i = 0; i < test.repl->N; i++)
    {
        MYSQL* conn = test.repl->nodes[i];
        test.try_query(conn, "UNINSTALL SONAME 'auth_pam';");
        test.repl->ssh_node_f(i, true, "%s", remove_user_cmd.c_str());
        test.repl->ssh_node_f(i, true, "%s", read_shadow_off.c_str());
        test.repl->ssh_node_f(i, true, "%s", delete_pam_conf_cmd.c_str());
        test.repl->ssh_node_f(i, true, "%s", delete_pam_message_cmd.c_str());
    }
    test.maxscales->ssh_node_f(0, true, "%s", remove_user_cmd.c_str());
    test.maxscales->ssh_node_f(0, true, "%s", read_shadow_off.c_str());
    test.maxscales->ssh_node_f(0, true, "%s", delete_pam_conf_cmd.c_str());
    test.maxscales->ssh_node_f(0, true, "%s", delete_pam_message_cmd.c_str());

    test.repl->disconnect();
    return test.global_result;
}
