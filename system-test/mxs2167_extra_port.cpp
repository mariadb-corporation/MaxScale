/**
 * MXS-2167: Monitors should be able to use extra_port
 */

#include <maxtest/testconnections.hh>
#include <iostream>

using std::cout;
using std::endl;
using std::string;

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    // The test requires at least three nodes.
    auto N = test.repl->N;
    if (N < 3)
    {
        test.expect(false, "Too few nodes, need at least 3.");
        return test.global_result;
    }

    test.tprintf("Stopping MaxScale");
    test.maxscale->stop();

    // Configure extra-port on servers 1&2.
    const int N_extra_port = 2;
    const int extra_port = 33066;
    const string extra_port_str = std::to_string(extra_port);
    const string iptables_cmd =
        "iptables -I INPUT -p tcp --dport " + extra_port_str + " -j ACCEPT;"
        + "ip6tables -I INPUT -p tcp --dport " + extra_port_str + " -j ACCEPT";

    const string iptables_remove_cmd =
        "iptables -D INPUT -p tcp --dport " + extra_port_str + " -j ACCEPT;"
        + "ip6tables -D INPUT -p tcp --dport " + extra_port_str + " -j ACCEPT";

    const string extra_port_sett = "extra_port=" + extra_port_str;

    string user = test.repl->user_name;
    string pw = test.repl->password;

    for (int i = 0; i < N_extra_port; i++)
    {
        test.tprintf("Configuring node %i for extra port.", i);
        // Temporary workaround for firewall issues
        test.repl->ssh_node_f(i, true, "%s", iptables_cmd.c_str());

        test.repl->stash_server_settings(i);
        test.repl->add_server_setting(i, extra_port_sett.c_str());
        test.repl->add_server_setting(i, "extra_max_connections=5");
        test.repl->ssh_node_f(i, true, "systemctl restart mariadb || service mariadb restart");

        // Test a direct connection to the server through the extra port, it should work.
        auto conn = open_conn_db_timeout(extra_port, test.repl->ip(i), "", user, pw, 4, false);
        int rc = test.try_query(conn, "SELECT 1;");
        test.expect(rc == 0, "Connection from host machine to node %i through port %i failed.",
                    i, extra_port);
        if (rc == 0)
        {
            test.tprintf("Extra port working on node %i.", i);
        }
        mysql_close(conn);
    }

    if (test.ok())
    {
        // Limit the number of connections on servers 1&2. Resets on restart.
        const int max_conns = 10;   // 10 is minimum allowed by server.
        const string set_max_conns = "SET GLOBAL max_connections=" + std::to_string(max_conns) + ";";
        test.repl->connect();
        for (int i = 0; i < N_extra_port; i++)
        {
            test.try_query(test.repl->nodes[i], "%s", set_max_conns.c_str());
            if (test.ok())
            {
                test.tprintf("Max connections limit set on node %i.", i);
            }
        }
        test.repl->disconnect();

        if (test.ok())
        {
            // Then, open connections until the limit is met. Should open a total of 20.
            // It seems this setting is not entirely accurate as sometimes one can open a few more.
            const int max_conns_limit = max_conns + 5;
            std::vector<MYSQL*> connections;
            for (int i = 0; i < N_extra_port; i++)
            {
                test.tprintf("Opening connections on node %i until maximum reached.", i);
                int normal_port = test.repl->port[i];
                auto host = test.repl->ip(i);

                int conn_count = 0;
                while (conn_count < max_conns_limit)
                {
                    auto conn = open_conn_db_timeout(normal_port, host, "", user, pw, 4, false);
                    if (conn)
                    {
                        if (execute_query_silent(conn, "SELECT 1") == 0)
                        {
                            connections.push_back(conn);
                            conn_count++;
                        }
                        else
                        {
                            mysql_close(conn);
                            break;
                        }
                    }
                    else
                    {
                        break;
                    }
                }

                bool conns_ok = conn_count >= max_conns && conn_count <= max_conns_limit;
                if (conns_ok)
                {
                    test.tprintf("Opened %i connections to node %i.", conn_count, i);
                }
                else
                {
                    test.expect(false, "Opened %i connections to node %i when %i--%i expected.",
                                conn_count, i, max_conns, max_conns_limit);
                }
            }

            if (test.ok())
            {
                // Finally, start MaxScale. The monitor should use extra port to connect to nodes 0&1,
                // and normal port to connect to 2&3. All servers should be running.
                cout << "Starting MaxScale" << endl;
                test.maxscale->start();
                sleep(3);   // Give maxscale some time to start properly.
                test.maxscale->wait_for_monitor(2);
                for (int i = 0; i < N; i++)
                {
                    string server_name = "server" + std::to_string(i + 1);
                    auto srv_namez = server_name.c_str();
                    auto status = test.maxscale->get_server_status(srv_namez);
                    bool status_ok = status.count("Running") == 1;
                    if (status_ok)
                    {
                        string status_str;
                        for (auto s : status)
                        {
                            status_str += s + ",";
                        }
                        test.tprintf("%s status is: %s", srv_namez, status_str.c_str());
                    }
                    test.expect(status.count("Running") == 1, "Server '%s' is not running or monitor could "
                                                              "not connect to it.", srv_namez);
                    // Also, MaxScale should have used the extra port to connect to nodes 0 & 1.
                    if (i < N_extra_port)
                    {
                        string pat = "Could not connect with normal port to server '" + server_name
                            + "', using extra_port";
                        test.log_includes(pat.c_str());
                    }
                }

                if (test.ok())
                {
                    // Creating sessions should not work since normal connections cannot be created to
                    // the master node.
                    auto conn = test.maxscale->open_rwsplit_connection();
                    if (!conn)
                    {
                        test.tprintf("Session creation failed, as expected.");
                    }
                    else if (execute_query_silent(conn, "SELECT 1;") == 1)
                    {
                        test.tprintf("Query failed, as expected.");
                    }
                    else
                    {
                        test.expect(false, "Routing sessions should not work.");
                    }

                    if (conn)
                    {
                        mysql_close(conn);
                    }
                }
            }

            // Make sure the old connections still work and close them.
            for (auto conn : connections)
            {
                test.try_query(conn, "SELECT 2");
                mysql_close(conn);
            }
        }
    }

    // Change server configuration such that the primary port is wrong. Monitoring should still work.
    if (test.ok())
    {
        string srv_name = "server1";
        test.maxctrl("alter server " + srv_name + " port 12345");
        test.maxscale->wait_for_monitor(2);
        auto status = test.maxscale->get_server_status(srv_name);
        test.expect(status.count("Running") == 1, "Monitoring of %s through extra-port failed when normal "
                                                  "port disabled", srv_name.c_str());
        test.maxctrl("alter server " + srv_name + " port " + std::to_string(test.repl->port[0]));
    }

    // Remove extra_port
    for (int i = 0; i < N_extra_port; i++)
    {
        test.tprintf("Removing extra port from node %i.", i);
        test.repl->ssh_node_f(i, true, "%s", iptables_remove_cmd.c_str());

        test.repl->restore_server_settings(i);
        test.repl->ssh_node_f(i, true, "systemctl restart mariadb || service mariadb restart");
    }
    return test.global_result;
}
