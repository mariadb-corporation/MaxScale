/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <string>
#include <maxbase/format.hh>
#include <maxtest/mariadb_connector.hh>

/**
 * The test uses pre-generated ticket cache and keytab files. The cached ticket is valid for several years.
 * To create a new ticket, you need a running Kerberos-server. This is easiest with a docker-image, e.g.
 * https://hub.docker.com/r/gcavalcante8808/krb5-server/ Read the instructions for setup. Once the server is
 * running, create the principals with kadmin. You may want to change ticket lifetime limits. Export server
 * key to a keytab-file. Then, create a ticket for accessing the service with kinit. Give the service name
 * and ticket lifetime as parameters. For more details, see "man kinit" or Kerberos help. kinit saves the
 * ticket cache to /tmp, just copy from there.
 */
using std::string;

namespace
{
const char KEYTAB_SVR_DST[] = "/tmp/mariadb.keytab";
string keytab_src = string(mxt::SOURCE_DIR) + "/authentication/gss_mariadb.keytab";
string del_cmd = string("rm -f ") + KEYTAB_SVR_DST;
}

void test_main(TestConnections& test);
void prepare_server_gss(TestConnections& test, int node);
void cleanup_server_gss(TestConnections& test, int node);

int main(int argc, char** argv)
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& repl = *test.repl;
    auto& mxs = test.maxscale;

    uint32_t uid = getuid();
    test.expect(uid > 0, "Failed to read user uid.");

    // Delete any currently existing tickets, perhaps left over from a previous test.
    test.run_shell_cmd_output("kdestroy");

    if (test.ok())
    {
        string ticket_cache_src = string(mxt::SOURCE_DIR) + "/authentication/gss_client_ticket_cache";
        // Copy the pregenerated Kerberos ticket cache used by the client to the default file. This way,
        // the local krb-library finds it when connecting to MaxScale. Gss-client libraries look for ticket
        // cache from the following filename.
        string ticket_cache_dst = mxb::string_printf("/tmp/krb5cc_%u", uid);
        string copy_cmd = mxb::string_printf("cp %s %s",
                                             ticket_cache_src.c_str(), ticket_cache_dst.c_str());
        test.tprintf("Copying ticket cache to %s", ticket_cache_dst.c_str());
        test.run_shell_cmd_output(copy_cmd, "Failed to copy ticket cache file.");
        auto res = test.run_shell_cmd_output("klist", "Failed to read ticket cache.");
        test.tprintf("klist output:\n%s", res.output.c_str());

        mxs->copy_to_node(keytab_src.c_str(), KEYTAB_SVR_DST);
        prepare_server_gss(test, 0);
        prepare_server_gss(test, 1);

        mxs->sleep_and_wait_for_monitor(2, 2);
        mxs->check_print_servers_status({mxt::ServerInfo::master_st, mxt::ServerInfo::slave_st});

        if (test.ok())
        {
            const char username[] = "gssuser";
            const char gss_realm[] = "TEST.COM";
            auto conn = repl.backend(0)->open_connection();

            auto test_query = [&](bool expected) {
                    // To ensure MaxScale has updated user accounts, try to log in with a non-existent user.
                    mxs->try_open_rwsplit_connection("batman", "iambatman");

                    // Use local command line client to log in and perform a query, as connector-c may not
                    // support gssapi.
                    const char expected_res[] = "12345";
                    string mysql_cmd = mxb::string_printf(
                        "mysql --host=%s --port=%i --user=%s -N -s -e \"select %s;\"",
                        mxs->ip(), mxs->rwsplit_port, username, expected_res);
                    if (expected)
                    {
                        res = test.run_shell_cmd_output(mysql_cmd, "Login or query failed.");
                        if (res.rc == 0 && res.output != expected_res)
                        {
                            test.add_failure("Unexpected query result: '%s'.", res.output.c_str());
                        }
                    }
                    else
                    {
                        res = test.shared().run_shell_cmd_output(mysql_cmd);
                        test.expect(res.rc != 0, "Login and query succeeded when they should have failed.");
                    }
                };

            string create_p1 = mxb::string_printf("create user '%s' identified via gssapi", username);
            string drop_cmd = mxb::string_printf("drop user '%s';", username);
            if (test.ok())
            {
                test.tprintf("Testing user account with defined authentication_string.");
                conn->cmd_f("%s using '%s@%s';", create_p1.c_str(), username, gss_realm);
                test_query(true);
                conn->cmd(drop_cmd);
            }

            if (test.ok())
            {
                test.tprintf("Testing user account without authentication_string.");
                conn->cmd_f("%s;", create_p1.c_str());
                test_query(true);
                conn->cmd(drop_cmd);
            }

            if (test.ok())
            {
                test.tprintf("Testing user account with faulty authentication_string.");
                conn->cmd_f("%s using 'different_user@%s';", create_p1.c_str(), gss_realm);
                test_query(false);
                conn->cmd(drop_cmd);
                // Check from log that MaxScale blocked the login.
                test.log_includes("\\[GSSAPIAuth\\] Name mismatch: found 'gssuser@TEST.COM'");
            }
        }

        cleanup_server_gss(test, 0);
        cleanup_server_gss(test, 1);
        mxs->vm_node().run_cmd_output_sudo(del_cmd);
        mxs->sleep_and_wait_for_monitor(2, 2);
        mxs->check_print_servers_status({mxt::ServerInfo::master_st, mxt::ServerInfo::slave_st});

        // Delete tickets.
        test.run_shell_cmd_output("kdestroy");
    }
}

void prepare_server_gss(TestConnections& test, int node)
{
    auto& repl = *test.repl;
    auto be = repl.backend(node);
    test.tprintf("Preparing %s for gssapi.", be->cnf_name().c_str());
    be->stop_database();
    be->vm_node().run_cmd_output_sudo("yum -y install MariaDB-gssapi-server");
    repl.copy_to_node(node, keytab_src.c_str(), KEYTAB_SVR_DST);
    repl.stash_server_settings(node);
    repl.add_server_setting(node, "plugin_load_add=auth_gssapi");
    string setting = string("gssapi_keytab_path=") + KEYTAB_SVR_DST;
    repl.add_server_setting(node, setting.c_str());
    repl.add_server_setting(node, "gssapi_principal_name=mariadb@TEST.COM");
    be->start_database();
    test.tprintf("Preparation done.");
}

void cleanup_server_gss(TestConnections& test, int node)
{
    auto& repl = *test.repl;
    auto be = repl.backend(node);
    test.tprintf("Cleaning up %s from gssapi.", be->cnf_name().c_str());
    be->stop_database();
    repl.backend(node)->vm_node().run_cmd_output_sudo(del_cmd);
    repl.restore_server_settings(node);
    be->start_database();
    test.tprintf("Cleanup done.");
}
