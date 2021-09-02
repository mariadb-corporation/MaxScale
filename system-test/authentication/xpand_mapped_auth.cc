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

const char users_file[] = "xpand_mapped_auth_users.json";

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    TestConnections test;
    TestConnections::skip_maxscale_start(true);
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto test_dir = mxt::SOURCE_DIR;
    auto& mxs = *test.maxscale;
    auto& mxs_vm = mxs.vm_node();
    auto& xpand = *test.xpand;

    // Copy user accounts file to MaxScale VM.
    string accounts_file_src = mxb::string_printf("%s/authentication/%s", test_dir, users_file);
    string accounts_file_dst = mxb::string_printf("/tmp/%s", users_file);
    mxs.vm_node().copy_to_node(accounts_file_src, accounts_file_dst);
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
    }

    if (test.ok())
    {
        // Test user mapping.
        const string orig_user = "orig_pam_user";
        const string orig_pass = "orig_pam_pw";

        // Copy the pam mapping module to the MaxScale VM. Also copy pam service config and mapping config.
        pam::copy_user_map_lib(test.repl->backend(0)->vm_node(), mxs_vm);
        pam::copy_map_config(mxs_vm);
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
            }
        }

        if (test.ok())
        {
            // Next, test user when the final user is with password. TODO
        }

        mxs_vm.remove_linux_user(orig_user);
        pam::delete_map_config(mxs_vm);
        pam::delete_user_map_lib(mxs_vm);
    }

    // Delete accounts file.
    mxs_vm.delete_from_node(accounts_file_dst);
}
