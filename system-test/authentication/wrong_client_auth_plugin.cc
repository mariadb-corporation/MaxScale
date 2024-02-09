/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

// Try to connect with mysql client using the plugin "mysql_clear_password". MaxScale should switch back
// to "mysql_native_password".

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto try_conn = [&test](const char* host, int port, const char* user, const char* pass) {
        MYSQL* maxconn = mysql_init(NULL);
        // Need to set plugin directory so that mysql_clear_password is found.
        const char plugin_path[] = "../../connector-c/install/lib/mariadb/plugin";
        const char plugin[] = "caching_sha2_password";
        mysql_optionsv(maxconn, MYSQL_PLUGIN_DIR, plugin_path);
        mysql_optionsv(maxconn, MYSQL_DEFAULT_AUTH, plugin);
        test.tprintf("Trying to log in to [%s]:%i as %s with plugin '%s' and password '%s'.",
                     host, port, user, plugin, pass);
        mysql_real_connect(maxconn, host, user, pass, NULL, port, NULL, 0);
        auto err = mysql_error(maxconn);
        if (*err)
        {
            test.add_failure("Could not log in: '%s'", err);
        }
        else
        {
            test.try_query(maxconn, "SELECT rand();");
            if (test.ok())
            {
                test.tprintf("Logged in and queried successfully.\n");
            }
            else
            {
                test.tprintf("Query rejected: '%s'\n", mysql_error(maxconn));
            }
        }
        mysql_close(maxconn);
    };

    auto& mxs = *test.maxscale;
    const char* host = mxs.ip4();
    int port = mxs.ports[0];
    const char* user = mxs.user_name().c_str();
    const char* pass = mxs.password().c_str();

    try_conn(host, port, user, pass);

    if (test.ok())
    {
        // Create a user with no password. Check that can log in while giving wrong auth plugin. Tests
        // MXS-4094.
        auto admin_conn = test.repl->backend(0)->admin_connection();
        std::string username = "batman";
        mxt::ScopedUser no_pw_user = admin_conn->create_user(username, "%", "");
        no_pw_user.grant("select on test.*");
        try_conn(host, port, username.c_str(), "");
    }
}
