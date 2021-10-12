/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

// Try to connect with mysql client using the plugin "mysql_clear_password". MaxScale should switch back
// to "mysql_native_password".

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    const char* host = test.maxscale->ip4();
    int port = test.maxscale->ports[0];
    const char* user = test.maxscale->user_name().c_str();
    const char* pass = test.maxscale->password().c_str();
    const char plugin[] = "mysql_clear_password";

    test.tprintf("Trying to log in to [%s]:%i as %s with plugin '%s'.\n", host, port, user, plugin);
    MYSQL* maxconn = mysql_init(NULL);
    test.expect(maxconn, "mysql_init failed");
    if (maxconn)
    {
        // Need to set plugin directory so that mysql_clear_password is found.
        const char plugin_path[] = "../connector-c/install/lib/mariadb/plugin";
        mysql_optionsv(maxconn, MYSQL_PLUGIN_DIR, plugin_path);
        mysql_optionsv(maxconn, MYSQL_DEFAULT_AUTH, plugin);
        mysql_real_connect(maxconn, host, user, pass, NULL, port, NULL, 0);
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
                test.tprintf("Logged in and queried successfully.\n");
                test.log_includes("is using an unsupported authenticator plugin 'mysql_clear_password'.");
            }
            else
            {
                test.tprintf("Query rejected: '%s'\n", mysql_error(maxconn));
            }
        }
        mysql_close(maxconn);
    }


    return test.global_result;
}
