/**
 * @file proxy_protocol.cpp proxy protocol test
 *
 * Proxy protocol is enabled in MaxScale config. Enable it on the server, then create a user which has only
 * the client ip in its allowed hosts. Check that user can log in directly to server and through MaxScale.
 */


#include <iostream>
#include <string>
#include "testconnections.h"

using std::string;
using std::cout;

int main(int argc, char *argv[])
{
    TestConnections::require_repl_version("10.3.1"); // Proxy protocol support is rather new.
    TestConnections test(argc, argv);
    test.repl->connect();

    const string maxscale_ip = test.maxscales->IP[0];
    const int maxscale_port = test.maxscales->rwsplit_port[0];

    // Router sessions shouldn't work, since MaxScale is sending the proxy header even when
    // server is not expecting it. The connection to MaxScale is created but queries will fail.
    auto adminconn = test.maxscales->open_rwsplit_connection(0);
    test.expect(adminconn != NULL, "Connection to MaxScale with user %s failed when success was expected.",
                test.maxscales->user_name);
    if (adminconn)
    {
        test.expect(execute_query(adminconn, "SELECT 1") != 0,
                    "Query with user %s succeeded when failure was expected.", test.maxscales->user_name);
        mysql_close(adminconn);
        adminconn = NULL;
    }

    bool server_proxy_setting = false;
    // Activate proxy protocol on the server now, otherwise router sessions won't work.
    if (test.global_result == 0)
    {
        cout << "Setting up proxy protocol on server1.\n";
        // Configure proxy protocol on the server.
        test.repl->stop_node(0);
        test.repl->stash_server_settings(0);

        string proxy_setting = string("proxy_protocol_networks =  ") + maxscale_ip;
        test.repl->add_server_setting(0, proxy_setting.c_str());
        test.repl->add_server_setting(0, "skip-name-resolve=1"); // To disable server hostname resolution.
        test.repl->start_node(0, (char *) "");
        cout << "Proxy protocol set.\n";
        test.maxscales->wait_for_monitor(2); // Wait for server to start and be detected
        test.repl->connect();
        server_proxy_setting = true;
    }

    // Check what is the client ip.
    string client_ip;
    if (test.global_result == 0)
    {
        int bufsize = 512;
        char client_userhost[bufsize];
        // Send the user query directly to backend.
        if (find_field(test.repl->nodes[0], "SELECT USER();", "USER()", client_userhost) == 0)
        {
            client_ip = strstr(client_userhost, "@") + 1;
            cout << "Client IP is " << client_ip << "\n";
            cout << "MaxScale IP is " << maxscale_ip << " and port is " << maxscale_port << "\n";
            cout << "Server IP is " << test.repl->IP[0] << "\n";
        }
        else
        {
            test.expect(false, "Could not read client ip.");
        }
    }

    const string username = "proxy_user";
    const string userpass = "proxy_pwd";
    if (test.global_result == 0)
    {
        adminconn = test.maxscales->open_rwsplit_connection(0);
        test.expect(adminconn, "MaxScale connection failed.");
        if (adminconn)
        {
            // Remove any existing conflicting user names, should not exist.
            cout << "Removing any leftover users, these queries may fail.\n";
            execute_query(adminconn, "DROP USER IF EXISTS '%s'@'%%'", username.c_str());
            execute_query(adminconn, "DROP USER IF EXISTS '%s'@'%s'", username.c_str(), maxscale_ip.c_str());
            execute_query(adminconn, "DROP USER IF EXISTS '%s'@'%s'", username.c_str(), client_ip.c_str());
            mysql_close(adminconn);
            adminconn = NULL;

            // Try to connect through MaxScale using the proxy-user, it shouldn't work.
            auto testconn = open_conn(maxscale_port, maxscale_ip, username, userpass);
            if (testconn)
            {
               test.expect(execute_query(testconn, "SELECT 1") != 0,
                           "Query with user %s succeeded when failure was expected.", username.c_str());
               mysql_close(testconn);
            }
        }
    }

    if (test.global_result == 0)
    {
        adminconn = test.maxscales->open_rwsplit_connection(0);
        test.expect(adminconn, "MaxScale connection failed.");
        if (adminconn)
        {
            // Create a test table and the proxy user.
            cout << "Creating user '"<< username << "' \n";
            test.try_query(adminconn, "CREATE OR REPLACE TABLE test.t1(id INT)");
            test.try_query(adminconn, "CREATE USER '%s'@'%s' identified by '%s'",
                           username.c_str(), client_ip.c_str(), userpass.c_str());
            test.try_query(adminconn, "GRANT SELECT,INSERT ON test.t1 TO '%s'@'%s'", username.c_str(), client_ip.c_str());
            test.try_query(adminconn, "FLUSH PRIVILEGES;");
            if (test.global_result == 0)
            {
                cout << "User created\n";
            }
            else
            {
                cout << "User creation or related query failed.\n";
            }
            mysql_close(adminconn);
        }
    }

    // Try the user by connecting directly to the server, it should work.
    auto testconn = open_conn(test.repl->port[0], test.repl->IP[0], username, userpass);
    test.expect(testconn != NULL, "Connection to server1 failed when success was expected.");
    if (testconn)
    {
        mysql_close(testconn);
        testconn = NULL;
    }

    if (test.global_result == 0)
    {
        // The test user should be able to login also through MaxScale.
        testconn = open_conn(maxscale_port, maxscale_ip, username, userpass);
        test.expect(testconn != NULL, "Connection to MaxScale failed when success was expected.");
        if (testconn)
        {
            // Try some queries to ensure it's working.
            test.try_query(testconn, "INSERT INTO test.t1 VALUES (232);");
            test.try_query(testconn, "INSERT INTO test.t1 VALUES (323);");
            int expected_rows = 2;
            int found_rows = execute_query_count_rows(testconn, "SELECT * FROM test.t1;");
            test.expect(found_rows == expected_rows, "Unexpected query results.");
            mysql_close(testconn);
            if (test.global_result == 0)
            {
                cout << "Results were as expected, test successful.\n";
            }
        }
        // Use the superuser to remove the test user.
        adminconn = test.maxscales->open_rwsplit_connection(0);
        test.expect(adminconn, "MaxScale connection failed.");
        if (adminconn)
        {
            cout << "Removing test user.\n";
            test.try_query(adminconn, "DROP TABLE IF EXISTS test.t1");
            test.try_query(adminconn, "DROP USER '%s'@'%s'", username.c_str(), client_ip.c_str());
            mysql_close(adminconn);
            adminconn = NULL;
        }
    }


    /**
     * MXS-2252: Proxy Protocol not displaying originating IP address in SHOW PROCESSLIST
     * https://jira.mariadb.org/browse/MXS-2252
     */
    Connection direct = test.repl->get_connection(0);
    Connection rwsplit = test.maxscales->rwsplit(0);
    direct.connect();
    rwsplit.connect();
    auto d = direct.field("SELECT USER()");
    auto r = rwsplit.field("SELECT USER()");
    test.tprintf("Direct: %s Readwritesplit: %s", d.c_str(), r.c_str());
    test.expect(d == r, "Both connections should return the same user: %s != %s", d.c_str(), r.c_str());

    if (server_proxy_setting)
    {
        // Restore server settings.
        cout << "Removing proxy setting from server1.\n";
        test.repl->stop_node(0);
        test.repl->restore_server_settings(0);
        test.repl->start_node(0, (char *) "");
        test.maxscales->wait_for_monitor(2);
        server_proxy_setting = false;
    }

    test.repl->disconnect();
    return test.global_result;
}

