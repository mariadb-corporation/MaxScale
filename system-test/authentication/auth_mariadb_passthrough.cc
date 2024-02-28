/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>
#include "auth_utils.hh"

using std::string;

namespace
{
using Ssl = auth_utils::Ssl;

void test_change_user(TestConnections& test, Ssl ssl, int port, const char* user1, const char* pw1,
                      const char* user2, const char* pw2)
{
    const char change_failed[] = "COM_CHANGE_USER from %s to %s failed.";
    auto mxs_ssl = ssl == Ssl::ON ? mxt::MaxScale::SslMode::ON : mxt::MaxScale::SslMode::OFF;
    auto conn1 = test.maxscale->try_open_connection(mxs_ssl, port, user1, pw1);
    bool ok1 = conn1->change_user(user2, pw2, "");
    test.expect(ok1, change_failed, user1, user2);
    // Try the other way around
    auto conn2 = test.maxscale->try_open_connection(mxs_ssl, port, user2, pw2);
    bool ok2 = conn2->change_user(user1, pw1, "");
    test.expect(ok2, change_failed, user2, user1);
    if (ok1 && ok2)
    {
        test.tprintf("COM_CHANGE_USER %s<-->%s succeeded.", user1, user2);
    }
}

void test_main(TestConnections& test)
{
    auto& repl = *test.repl;
    auto& mxs = *test.maxscale;
    auto* master_srv = repl.backend(0);
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        auto admin_conn = master_srv->admin_connection();

        // Test with users identified with native_password.
        const char user1[] = "native_user";
        const char pw1[] = "password1";
        auto pass_user = admin_conn->create_user(user1, "", pw1);

        const char user2[] = "native_user2";
        const char pw2[] = "password2";
        auto pass_user2 = admin_conn->create_user(user2, "", pw2);

        const char no_pass[] = "native_nopass";
        auto no_pass_user = admin_conn->create_user(no_pass, "", "");

        repl.sync_slaves();

        if (test.ok())
        {
            auto test_login = [&](int port, Ssl ssl) {
                try_conn(test, port, ssl, user1, pw1, true);
                try_conn(test, port, ssl, user2, pw2, true);
                try_conn(test, port, ssl, user1, "wrong", false);
                try_conn(test, port, ssl, no_pass, "", true);

                // Test change user (MXS-4723)
                if (test.ok())
                {
                    test_change_user(test, ssl, port, user1, pw1, user2, pw2);
                    test_change_user(test, ssl, port, user1, pw1, no_pass, "");
                }
            };
            test.tprintf("Testing mysql_native_password, ssl OFF.");
            test_login(4006, Ssl::OFF);

            test.tprintf("Testing mysql_native_password, ssl is ON.");
            test_login(4007, Ssl::ON);
        }
    }


    if (test.ok())
    {
        // Setup pam on server1, with pam-use-cleartext-plugin. This "fools" the server into asking for
        // cleartext password, similar to Xpand with LDAP-users.
        master_srv->stop_database();
        master_srv->stash_server_settings();
        master_srv->add_server_setting("plugin_load_add = auth_pam");
        master_srv->add_server_setting("pam-use-cleartext-plugin=ON");
        auth_utils::copy_basic_pam_cfg(master_srv->vm_node());
        master_srv->start_database();
        sleep(1);
        repl.ping_or_open_admin_connections();

        // Create users
        const char pam_user[] = "pam_user";
        const char pam_pw[] = "pam_password";
        auth_utils::create_basic_pam_user(master_srv, pam_user);
        master_srv->vm_node().add_linux_user(pam_user, pam_pw);

        const char pam_user2[] = "pam_user2";
        const char pam_pw2[] = "pam_password2";
        auth_utils::create_basic_pam_user(master_srv, pam_user2);
        master_srv->vm_node().add_linux_user(pam_user2, pam_pw2);

        const char pam_no_pass[] = "pam_nopass";
        auth_utils::create_basic_pam_user(master_srv, pam_no_pass);
        master_srv->vm_node().add_linux_user(pam_no_pass, "");

        mxs.wait_for_monitor();
        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

        if (test.ok())
        {
            auto test_login = [&](int port, Ssl ssl) {
                try_conn(test, port, ssl, pam_user, pam_pw, true);
                try_conn(test, port, ssl, pam_user2, pam_pw2, true);
                try_conn(test, port, ssl, pam_user, "wrong", false);
                try_conn(test, port, ssl, pam_no_pass, "", true);

                // Test change user (MXS-4723)
                if (test.ok())
                {
                    test_change_user(test, ssl, port, pam_user, pam_pw, pam_user2, pam_pw2);
                    test_change_user(test, ssl, port, pam_user, pam_pw, pam_no_pass, "");
                }
            };

            test_login(4008, Ssl::OFF);
            test_login(4009, Ssl::ON);
        }

        // Cleanup users
        auth_utils::delete_basic_pam_user(master_srv, pam_user);
        master_srv->vm_node().remove_linux_user(pam_user);

        auth_utils::delete_basic_pam_user(master_srv, pam_user2);
        master_srv->vm_node().remove_linux_user(pam_user2);

        auth_utils::delete_basic_pam_user(master_srv, pam_no_pass);
        master_srv->vm_node().remove_linux_user(pam_no_pass);

        // Cleanup pam settings.
        master_srv->stop_database();
        master_srv->restore_server_settings();
        auth_utils::remove_basic_pam_cfg(master_srv->vm_node());
        master_srv->start_database();
    }
}
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
