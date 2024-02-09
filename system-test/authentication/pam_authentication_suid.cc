/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include "auth_utils.hh"

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    const char user[] = "my_pam_user";
    const char pw[] = "my_pam_pw";
    int rwsplit_port = 4006;

    // Use just two servers for this test.
    mxs.check_print_servers_status({mxt::ServerInfo::master_st, mxt::ServerInfo::slave_st});
    if (test.ok())
    {
        auto master_srv = repl.backend(0);
        auto slave_srv = repl.backend(1);
        auto srvs = {master_srv, slave_srv};

        for (auto srv : srvs)
        {
            auth_utils::install_pam_plugin(srv);
            auth_utils::copy_basic_pam_cfg(srv->vm_node());
        }
        auth_utils::copy_basic_pam_cfg(mxs.vm_node());
        auth_utils::prepare_basic_pam_user(user, pw, &mxs, master_srv, {slave_srv});

        if (test.ok())
        {
            test.tprintf("PAM preparations complete, trying to login.");
            auth_utils::try_conn(test, rwsplit_port, auth_utils::Ssl::OFF, user, pw, true);
            auth_utils::try_conn(test, rwsplit_port, auth_utils::Ssl::OFF, user, "wrong", false);
        }

        auth_utils::remove_pam_user(user, &mxs, master_srv, {slave_srv});
        auth_utils::remove_basic_pam_cfg(mxs.vm_node());
        for (auto srv : srvs)
        {
            auth_utils::remove_basic_pam_cfg(srv->vm_node());
            auth_utils::uninstall_pam_plugin(srv);
        }
    }
}
}
int main(int argc, char** argv)
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}
