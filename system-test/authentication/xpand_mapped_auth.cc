/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
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

int main(int argc, char** argv)
{
    TestConnections test;
    TestConnections::skip_maxscale_start(true);
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    const char users_file[] = "xpand_mapped_auth_users.json";
    const char pwds_file[] = "xpand_mapped_auth_pwds.json";

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

    copy_secrets(test);
    mxs.start_and_check_started();

    // Wait a little to allow Xpand-Monitor to discover servers.
    mxs.sleep_and_wait_for_monitor(2, 2);
    auto master = mxt::ServerInfo::master_st;
    mxs.check_print_servers_status({master, master, master, master});

    if (test.ok())
    {
        // First, check that service correctly reads an extra user from file. 'batman' does not exist on
        // backend, so queries will fail.
        auto conn = mxs.try_open_rwsplit_connection("batman", "iambatman");
        test.expect(conn->is_open(), "Login to file-based user failed.");
        auto res = conn->try_query("select rand();");
        test.expect(!res, "Query succeeded when it should have failed.");
        if (test.ok())
        {
            test.tprintf("Reading users from external file works.");
        }
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

                auto test_user = [&test, &mxs](const string& user, const string& pw,
                                               const string& final_user) {
                        auto conn = mxs.try_open_rwsplit_connection(user, pw);
                        if (conn->is_open())
                        {
                            auto res = conn->query("select current_user()");
                            if (res && res->next_row())
                            {
                                string found_user = res->get_string(0);
                                string expected = mxb::string_printf("'%s'@'%s'",
                                                                     final_user.c_str(), mxs.ip_private());
                                if (found_user == expected)
                                {
                                    test.tprintf(
                                        "Original user '%s' logged in and mapped to '%s', as expected.",
                                        user.c_str(), final_user.c_str());
                                }
                                else
                                {
                                    test.add_failure("Unexpected final user. Found %s, expected '%s'",
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
                    };
                test_user(grp1_user1, grp1_pw1, final_user1);
                test_user(grp1_user2, grp1_pw2, final_user1);
                test_user(grp2_user1, grp2_pw1, final_user2);
            }

            cleanup_grp_test(test);
        }

        pam::delete_map_config(mxs_vm);
        pam::delete_user_map_lib(mxs_vm);
    }

    // Delete accounts file, passwords file and secrets.
    mxs_vm.delete_from_node(accounts_file_dst);
    mxs_vm.delete_from_node(passwords_file_dst);
    mxs_vm.delete_from_node(secrets_file_dst);
}

void prepare_grp_test(TestConnections& test)
{
    auto& mxs_vm = test.maxscale->vm_node();
    // Add some more Linux users and assign them to groups.
    const char add_grp_fmt[] = "groupadd %s";
    auto res1 = mxs_vm.run_cmd_output_sudof(add_grp_fmt, grp1_name);
    auto res2 = mxs_vm.run_cmd_output_sudof(add_grp_fmt, grp2_name);
    test.expect(res1.rc == 0 && res2.rc == 0, "Group add failed");

    auto add_to_group = [&test, &mxs_vm](const string& grp, const string& user, const string& pw) {
            mxs_vm.add_linux_user(user, pw);
            auto res = mxs_vm.run_cmd_output_sudof("groupmems -a %s -g %s", user.c_str(), grp.c_str());
            test.expect(res.rc == 0, "Failed to add user to group: %s", res.output.c_str());
        };
    add_to_group(grp1_name, grp1_user1, grp1_pw1);
    add_to_group(grp1_name, grp1_user2, grp1_pw2);
    add_to_group(grp2_name, grp2_user1, grp2_pw1);
}

void cleanup_grp_test(TestConnections& test)
{
    auto& mxs_vm = test.maxscale->vm_node();

    mxs_vm.remove_linux_user(grp1_user1);
    mxs_vm.remove_linux_user(grp1_user2);
    mxs_vm.remove_linux_user(grp2_user1);

    const char del_grp_fmt[] = "groupdel %s";
    auto res1 = mxs_vm.run_cmd_output_sudof(del_grp_fmt, grp1_name);
    string grp2_cmd = mxb::string_printf(del_grp_fmt, grp2_name);
    auto res2 = mxs_vm.run_cmd_output_sudof(del_grp_fmt, grp2_name);
    test.expect(res1.rc == 0 && res2.rc == 0, "Group delete failed");
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
