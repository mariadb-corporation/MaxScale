/**
 * MXS-2167: Monitors should be able to use extra_port
 */

#include "testconnections.h"
#include <iostream>

using std::cout;
using std::endl;


int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    cout << "Stopping MaxScale" << endl;
    test.maxscales->stop();

    cout << "Configuring servers" << endl;
    // Add the extra_port parameter to all servers
    for (int i = 0; i < test.repl->N; i++)
    {
        test.repl->stash_server_settings(i);
        test.repl->add_server_setting(i, "extra_port=33066");
        test.repl->ssh_node_f(i, true, "service mysql restart");
    }

    test.repl->connect();
    test.try_query(test.repl->nodes[0], "%s", "CREATE USER 'monitor'@'%' IDENTIFIED BY 'monitor'");
    test.try_query(test.repl->nodes[0], "%s", "GRANT ALL ON *.* TO 'monitor'@'%'");
    test.repl->disconnect();

    cout << "Starting MaxScale" << endl;
    test.maxscales->start();

    // Stop the monitor to force the connections to be closed
    test.maxctrl("stop monitor MySQL-Monitor");

    // Limit the connections to 20 (is erased on restart)
    test.repl->connect();
    test.try_query(test.repl->nodes[0], "SET GLOBAL max_connections=20");
    test.repl->disconnect();

    std::vector<MYSQL*> connections;

    // Open connections until we hit the limit
    for (int i = 0; i < 40; i++)
    {
        cout << "Opening connection " << i << endl;

        MYSQL* conn = test.maxscales->open_rwsplit_connection();

        if (execute_query_silent(conn, "SELECT 1") == 0)
        {
            connections.push_back(conn);
        }
        else
        {
            mysql_close(conn);
            break;
        }
    }

    // Start the monitor to force it to reconnect
    test.maxctrl("start monitor MySQL-Monitor");
    test.maxscales->wait_for_monitor();

    // Make sure the old connections still work
    for (auto a : connections)
    {
        test.try_query(a, "SELECT 2");
        mysql_close(a);
    }

    cout << "Stopping MaxScale" << endl;
    test.maxscales->stop();

    // Remove extra_port
    for (int i = 0; i < test.repl->N; i++)
    {
        test.repl->restore_server_settings(i);
        test.repl->ssh_node_f(i, true, "service mysql restart");
    }

    test.repl->connect();
    test.try_query(test.repl->nodes[0], "%s", "DROP USER 'monitor'@'%'");
    test.repl->disconnect();

    return test.global_result;
}
