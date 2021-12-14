/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <string>
#include <maxbase/format.hh>
#include <maxtest/testconnections.hh>
#include <maxtest/execute_cmd.hh>
#include <maxtest/mariadb_connector.hh>

using std::string;
namespace
{
const char grp1_name[] = "test_group1";
const char grp2_name[] = "test_group2";
const char grp1_user1[] = "grp1_user1";
const char grp1_pw1[] = "grp1_pw1";
const char grp1_user2[] = "grp1_user2";
const char grp1_pw2[] = "grp1_pw2";
const char grp2_user1[] = "grp2_user1";
const char grp2_pw1[] = "grp2_pw1";

const char auth_dir_fmt[] = "%s/authentication/%s";
const char tmp_dir_fmt[] = "/tmp/%s";
const string secrets_file_dst = "/var/lib/maxscale/.secrets";
}

void test_main(TestConnections& test);
void prepare_grp_test(TestConnections& test);
void cleanup_grp_test(TestConnections& test);
void copy_secrets(TestConnections& test);
void test_user(TestConnections& test, int port, const string& user, const string& pw,
               const string& final_user, const string& final_host);

int main(int argc, char** argv)
{
    TestConnections test;
    TestConnections::skip_maxscale_start(true);
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    const char users_file[] = "xpand_mapped_auth_users.json";
    const char users_file2[] = "xpand_mapped_auth_users_manual.json";
    const char pwds_file[] = "xpand_mapped_auth_pwds.json";
    const char mapping_file[] = "xpand_mapped_auth_user_map.json";

    auto test_dir = mxt::SOURCE_DIR;
    auto& mxs = *test.maxscale;
    auto& mxs_vm = mxs.vm_node();
    auto& xpand = *test.xpand;

    // Copy user accounts and user passwords files to MaxScale VM.
    string accounts_file_src = mxb::string_printf(auth_dir_fmt, test_dir, users_file);
    string accounts_file_dst = mxb::string_printf(tmp_dir_fmt, users_file);
    mxs_vm.copy_to_node(accounts_file_src, accounts_file_dst);

    string passwords_file_src = mxb::string_printf(auth_dir_fmt, test_dir, pwds_file);
    string passwords_file_dst = mxb::string_printf(tmp_dir_fmt, pwds_file);
    mxs_vm.copy_to_node(passwords_file_src, passwords_file_dst);

    // Copy the second user accounts file and manual mapping file to MaxScale VM.
    string accounts_file2_src = mxb::string_printf(auth_dir_fmt, test_dir, users_file2);
    string accounts_file2_dst = mxb::string_printf(tmp_dir_fmt, users_file2);
    mxs_vm.copy_to_node(accounts_file2_src, accounts_file2_dst);

    string mapping_file_src = mxb::string_printf(auth_dir_fmt, test_dir, mapping_file);
    string mapping_file_dst = mxb::string_printf(tmp_dir_fmt, mapping_file);
    mxs_vm.copy_to_node(mapping_file_src, mapping_file_dst);

    copy_secrets(test);
    mxs.start_and_check_started();

    // Wait a little to allow Xpand-Monitor to discover servers.
    mxs.sleep_and_wait_for_monitor(2, 2);
    auto master = mxt::ServerInfo::master_st;
    mxs.check_print_servers_status({master, master, master, master});

    if (test.ok())
    {
        // First, check that service correctly reads an extra user as well as db grant and role from file.
        // 'batman' accesses 'test2' through a role.
        // 'batman' does not exist on backend, so queries will fail.
        const char db1[] = "test1";
        const char db2[] = "test2";
        const char user[] = "batman";
        const char pw[] = "iambatman";

        // Create databases for real so that MaxScale does not complain when logging in to them.
        auto server_conn = test.xpand->backend(0)->open_connection();
        server_conn->try_cmd_f("create database %s;", db1);
        server_conn->try_cmd_f("create database %s;", db2);
        mxs.maxctrl("reload service RWSplit-Router");
        sleep(1);

        auto conn = mxs.try_open_rwsplit_connection(user, pw, db1);
        test.expect(!conn->is_open(), "'%s' should not have access to '%s'", user, db1);
        conn = mxs.try_open_rwsplit_connection(user, pw, db2);
        test.expect(conn->is_open(), "'%s' should have access to '%s'", user, db2);

        auto res = conn->try_query("select rand();");
        test.expect(!res, "Query succeeded when it should have failed.");
        if (test.ok())
        {
            test.tprintf("Reading users from external file works.");
        }
        server_conn->cmd_f("drop database %s;", db1);
        server_conn->cmd_f("drop database %s;", db2);
    }

    if (test.ok())
    {
        test.tprintf("Prepare to test user mapping.");
        // Copy the pam mapping module to the MaxScale VM. Also copy pam service config and mapping config.
        pam::copy_user_map_lib(test.repl->backend(0)->vm_node(), mxs_vm);
        pam::copy_map_config(mxs_vm);

        if (test.ok())
        {
            // Test user mapping.
            const string orig_user = "orig_pam_user";
            const string orig_pass = "orig_pam_pw";
            mxs_vm.add_linux_user(orig_user, orig_pass);

            auto node_conn = xpand.backend(0)->open_connection();
            if (test.ok())
            {
                // First, test logging in when the final user is without password.
                const string mapped_username = "mapped_mariadb";
                auto mapped_user = node_conn->create_user_xpand(mapped_username, "%", "");
                auto conn = mxs.try_open_rwsplit_connection(orig_user, orig_pass);
                auto res = conn->query("select user()");
                test.expect(conn->is_open(), "Login as '%s' failed: %s", orig_user.c_str(), conn->error());
                test.expect(res && res->next_row(), "Query failed: %s", conn->error());
                if (test.ok())
                {
                    auto q_result = res->get_string(0);
                    test.expect(q_result.find(mapped_username) == 0,
                                "Query returned unexpected result: %s", q_result.c_str());
                    if (test.ok())
                    {
                        test.tprintf("Mapping to passwordless user works.");
                    }
                }
            }
            mxs_vm.remove_linux_user(orig_user);
        }

        if (test.ok())
        {
            // Next, test user when the final user is with password. Allow users to only log in from
            // MaxScale ip.
            test.tprintf("Prepare to test group mapping.");
            prepare_grp_test(test);

            if (test.ok())
            {
                const string final_user1 = "group_mapped_user1";
                const string final_user2 = "group_mapped_user2";
                auto node_conn = xpand.backend(0)->open_connection();
                auto mapped_pw_user1 = node_conn->create_user_xpand(final_user1, mxs.ip_private(),
                                                                    "group_mapped_pw1");
                auto mapped_pw_user2 = node_conn->create_user_xpand(final_user2, mxs.ip_private(),
                                                                    "group_mapped_pw2");

                test_user(test, mxs.rwsplit_port, grp1_user1, grp1_pw1, final_user1, mxs.ip_private());
                test_user(test, mxs.rwsplit_port, grp1_user2, grp1_pw2, final_user1, mxs.ip_private());
                test_user(test, mxs.rwsplit_port, grp2_user1, grp2_pw1, final_user2, mxs.ip_private());
            }

            cleanup_grp_test(test);
        }

        pam::delete_map_config(mxs_vm);
        pam::delete_user_map_lib(mxs_vm);
    }

    if (test.ok())
    {
        // Test the other listener. This listener uses manual mapping and a normal pam service. Normal
        // auth is also allowed. Users are not fetched from server.
        test.tprintf("Testing manually defined user/group mapping.");
        auto server_conn = test.xpand->backend(0)->open_connection();
        const char userA[] = "alpha";
        const char pwA[] = "pw_alpha";
        const char userD[] = "delta";
        const char pwD[] = "pw_delta";
        int mapper_service_port = 4007;

        auto user_alpha = server_conn->create_user_xpand(userA, "%", pwA);
        auto user_delta = server_conn->create_user_xpand(userD, "%", pwD);

        // First check that user 'alpha' works. This user is defined both in file and in server.
        test_user(test, mapper_service_port, userA, pwA, userA, "%");

        if (test.ok())
        {
            // Test 'beta'. Should map to 'delta'.
            test_user(test, mapper_service_port, "beta", "pw_beta", userD, "%");
            // Test 'gamma'. Logs in with pam, maps to 'delta'.
            const char userG[] = "gamma";
            const char pwG[] = "pw_gamma";
            mxs_vm.add_linux_user(userG, pwG);
            test_user(test, mapper_service_port, userG, pwG, userD, "%");
            mxs_vm.remove_linux_user(userG);
        }

        if (test.ok())
        {
            // Try Linux group based mapping. 'epsilon' does not exist in MaxScale, logs in through
            // anon user and maps to 'omega'.
            const char grpP[] = "psi";
            const char userE[] = "epsilon";
            const char pwE[] = "pw_epsilon";
            const char userO[] = "omega";
            const char pwO[] = "pw_omega";

            mxs_vm.add_linux_user(userE, pwE);
            mxs_vm.add_linux_group(grpP, {userE});
            auto user_omega = server_conn->create_user_xpand(userO, "%", pwO);
            test_user(test, mapper_service_port, userE, pwE, userO, "%");
            mxs_vm.remove_linux_group(grpP);
            mxs_vm.remove_linux_user(userE);
        }
    }

    // Delete accounts file, passwords file and secrets.
    mxs_vm.delete_from_node(accounts_file_dst);
    mxs_vm.delete_from_node(passwords_file_dst);
    mxs_vm.delete_from_node(accounts_file2_dst);
    mxs_vm.delete_from_node(mapping_file_dst);
    mxs_vm.delete_from_node(secrets_file_dst);
}

void prepare_grp_test(TestConnections& test)
{
    auto& mxs_vm = test.maxscale->vm_node();
    // Add some more Linux users and assign them to groups.
    mxs_vm.add_linux_user(grp1_user1, grp1_pw1);
    mxs_vm.add_linux_user(grp1_user2, grp1_pw2);
    mxs_vm.add_linux_user(grp2_user1, grp2_pw1);

    mxs_vm.add_linux_group(grp1_name, {grp1_user1, grp1_user2});
    mxs_vm.add_linux_group(grp2_name, {grp2_user1});
}

void cleanup_grp_test(TestConnections& test)
{
    auto& mxs_vm = test.maxscale->vm_node();

    mxs_vm.remove_linux_user(grp1_user1);
    mxs_vm.remove_linux_user(grp1_user2);
    mxs_vm.remove_linux_user(grp2_user1);

    mxs_vm.remove_linux_group(grp1_name);
    mxs_vm.remove_linux_group(grp2_name);
}

void copy_secrets(TestConnections& test)
{
    auto& mxs_vm = test.maxscale->vm_node();
    const char secrets_filename[] = "xpand_mapped_auth_secrets.json";
    // Copy secrets-file to MaxScale VM. Direct copy seems to fail, copy to temp first.
    string secrets_file_src = mxb::string_printf(auth_dir_fmt, mxt::SOURCE_DIR, secrets_filename);
    string secrets_file_tmp_dst = mxb::string_printf(tmp_dir_fmt, secrets_filename);
    mxs_vm.copy_to_node(secrets_file_src, secrets_file_tmp_dst);
    auto mv_res = mxs_vm.run_cmd_output_sudof("mv %s %s",
                                              secrets_file_tmp_dst.c_str(), secrets_file_dst.c_str());
    test.expect(mv_res.rc == 0, "File rename failed: %s", mv_res.output.c_str());
    // The .secrets-file requires specific permissions.
    mxs_vm.run_cmd_output_sudof("chown maxscale:maxscale %s", secrets_file_dst.c_str());
    mxs_vm.run_cmd_output_sudof("chmod u=r,g-rwx,o-rwx %s", secrets_file_dst.c_str());
}

void test_user(TestConnections& test, int port, const string& user, const string& pw,
               const string& final_user, const string& final_host)
{
    auto& mxs = *test.maxscale;
    auto conn = mxs.try_open_connection(port, user, pw);
    if (conn->is_open())
    {
        auto res = conn->query("select current_user()");
        if (res && res->next_row())
        {
            string found_user = res->get_string(0);
            string expected = mxb::string_printf("'%s'@'%s'",
                                                 final_user.c_str(), final_host.c_str());
            if (found_user == expected)
            {
                test.tprintf(
                    "Original user '%s' logged in and mapped to %s, as expected.",
                    user.c_str(), found_user.c_str());
            }
            else
            {
                test.add_failure("Unexpected final user. Found %s, expected %s",
                                 found_user.c_str(), expected.c_str());
            }
        }
        else
        {
            test.add_failure("Query failed.");
        }
    }
    else
    {
        test.add_failure("Login as '%s' failed.", user.c_str());
    }
}
