/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

using std::string;

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    TestConnections test;
    TestConnections::skip_maxscale_start(true);
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto try_conn = [&test](int port, bool ssl, const string& user, const string& pass,
                            const string& expected_user, bool expect_success) {
        mxt::MariaDB maxconn(test.logger());
        auto& sett = maxconn.connection_settings();
        sett.plugin_dir = "../../connector-c/install/lib/mariadb/plugin";
        sett.user = user;
        sett.password = pass;
        sett.ssl.enabled = ssl;

        const string& host = test.maxscale->ip4();

        test.tprintf("Trying to log in to [%s]:%i as '%s' using password '%s'.", host.c_str(), port,
                     user.c_str(), pass.c_str());
        bool connected = maxconn.try_open(host, port);
        if (connected)
        {
            if (expect_success)
            {
                auto res = maxconn.query("select rand();");
                if (res && res->next_row())
                {
                    auto user_res = maxconn.query("select user();");
                    if (user_res && user_res->next_row())
                    {
                        string found_user_host = user_res->get_string(0);
                        auto at_loc = found_user_host.find('@');
                        if (at_loc != string::npos)
                        {
                            string found_user = found_user_host.substr(0, at_loc);
                            test.expect(found_user == expected_user,
                                        "Wrong result from user query. Expected '%s'," "got '%s'.",
                                        expected_user.c_str(), found_user.c_str());
                        }
                        else
                        {
                            test.add_failure("Invalid result for user query.");
                        }
                    }
                    else
                    {
                        test.add_failure("User query failed: %s", maxconn.error());
                    }
                }
                else
                {
                    test.add_failure("Test query failed: %s", maxconn.error());
                }
            }
            else
            {
                test.add_failure("Connection to MaxScale succeeded when failure was expected.");
            }
        }
        else if (expect_success)
        {
            test.add_failure("Connection to MaxScale failed: %s", maxconn.error());
        }
        else
        {
            test.tprintf("Connection to MaxScale failed as expected.");
        }
    };

    auto& mxs = *test.maxscale;
    auto& mxs_vm = mxs.vm_node();
    auto& repl = *test.repl;

    // Ed25519 authentication requires mapping.
    auto test_dir = mxt::SOURCE_DIR;
    const char auth_dir_fmt[] = "%s/authentication/%s";
    const char tmp_dir_fmt[] = "/tmp/%s";
    const char mapping_file[] = "ed25519_auth_user_map.json";

    string mapping_file_src = mxb::string_printf(auth_dir_fmt, test_dir, mapping_file);
    string mapping_file_dst = mxb::string_printf(tmp_dir_fmt, mapping_file);
    mxs_vm.copy_to_node(mapping_file_src, mapping_file_dst);
    mxs.start_and_check_started();

    if (test.ok())
    {
        // Enable ed25519 plugin on all backends.
        repl.execute_query_all_nodes("INSTALL SONAME 'auth_ed25519';");

        auto admin_conn = repl.backend(0)->admin_connection();

        // Create main user and mapped user.
        string orig_ed_user = "supersecureuser";
        string orig_ed_pw = "RatherLongAnd53cur3P455w0rd_?*|.,";

        const char create_ed_user[] = "create or replace user %s identified via "
                                      "ed25519 using password('%s');";
        admin_conn->cmd_f(create_ed_user, orig_ed_user.c_str(), orig_ed_pw.c_str());

        string mapped_user = "lesssecureuser";
        string mapped_pass = "normalpw";
        admin_conn->cmd_f("create or replace user %s identified by '%s';",
                          mapped_user.c_str(), mapped_pass.c_str());
        repl.sync_slaves();

        const char drop_fmt[] = "drop user %s;";

        if (test.ok())
        {
            test.tprintf("Testing mapping to standard auth.");
            int mapped_port = 4006;
            try_conn(mapped_port, false, orig_ed_user, orig_ed_pw, mapped_user, true);
            try_conn(mapped_port, false, orig_ed_user, "this_is_a_wrong_password", mapped_user, false);

            test.tprintf("Testing self-mapping.");
            const string ed_user2 = "test_user2";
            const string ed_pw2 = "test_password2";
            admin_conn->cmd_f(create_ed_user, ed_user2.c_str(), ed_pw2.c_str());
            repl.sync_slaves();
            try_conn(mapped_port, false, ed_user2, ed_pw2, ed_user2, true);
            admin_conn->cmd_f(drop_fmt, ed_user2.c_str());
        }

        admin_conn->cmd_f(drop_fmt, mapped_user.c_str());
        admin_conn->cmd_f(drop_fmt, orig_ed_user.c_str());

        if (test.ok())
        {
            test.tprintf("Testing sha256-mode with ssl.");
            const string ed_sha_user = "sha_user";
            const string ed_sha_pw = "sha_password";
            admin_conn->cmd_f(create_ed_user, ed_sha_user.c_str(), ed_sha_pw.c_str());

            int sha256_port = 4007;
            try_conn(sha256_port, true, ed_sha_user, ed_sha_pw, ed_sha_user, true);
            admin_conn->cmd_f(drop_fmt, ed_sha_user.c_str());
        }

        repl.execute_query_all_nodes("UNINSTALL SONAME 'auth_ed25519';");
    }
}
