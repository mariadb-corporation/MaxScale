/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

using std::string;

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    const char uname[] = "ssl_required";
    const char pw[] = "ssl_pw";
    auto admin_conn = repl.backend(0)->admin_connection();
    auto ssl_user = admin_conn->create_user(uname, "%", pw);
    admin_conn->cmd_f("alter user '%s' require ssl", uname);
    repl.sync_slaves();

    auto test_port = [&](int port, bool ssl, bool expect_success) {
        auto ssl_mode = ssl ? mxt::MaxScale::SslMode::ON : mxt::MaxScale::SslMode::OFF;
        auto conn = mxs.try_open_connection(ssl_mode, port, uname, pw);
        if (expect_success)
        {
            test.expect(conn->is_open(), "Connection to %s listener failed.", ssl ? "SSL" : "normal");
            const char query[] = "select 1";
            auto res = conn->simple_query(query);
            test.tprintf("Query '%s' on %s listener returned '%s'.", query, ssl ? "SSL" : "normal",
                         res.c_str());
        }
        else
        {
            test.expect(!conn->is_open(), "Connection to %s listener succeeded when it should have failed.",
                        ssl ? "SSL" : "normal");
        }
    };

    const int normal_port = 4006;
    const int ssl_port = 4007;
    test.tprintf("User %s created. Attempting to log in to SSL listener.", uname);
    test_port(ssl_port, true, true);

    test.tprintf("Attempting to log in to a non-SSL listener.");
    test_port(normal_port, false, false);

    test.tprintf("Removing SSL-requirement from the user, it should work with the normal listener.");
    admin_conn->cmd_f("alter user '%s' require none", uname);
    repl.sync_slaves();
    test_port(normal_port, false, true);

    test.tprintf("Adding X509-requirement to the user, it should no longer work with the normal listener.");
    admin_conn->cmd_f("alter user '%s' require x509", uname);
    repl.sync_slaves();
    // Try to log in with a non-existing user to force user account refresh.
    mxs.try_open_rwsplit_connection("abc", "def");
    test_port(normal_port, false, false);
}
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
