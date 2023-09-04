/**
 * MXS-1678: Stopping IO thread on relay master causes it to be promoted as master
 *
 * https://jira.mariadb.org/browse/MXS-1678
 */
#include <maxtest/testconnections.hh>
#include <set>
#include <string>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.repl->connect();
    execute_query(test.repl->nodes[3], "STOP SLAVE");
    execute_query(test.repl->nodes[3],
                  "CHANGE MASTER TO MASTER_HOST='%s', MASTER_PORT=%d",
                  test.repl->ip_private(2),
                  test.repl->port[2]);
    execute_query(test.repl->nodes[3], "START SLAVE");
    sleep(5);

    StringSet master = {"Master", "Running"};
    StringSet slave = {"Slave", "Running"};
    StringSet running = {"Running"};
    StringSet relay_master = {"Relay Master", "Slave", "Running"};
    StringSet relay_master_only = {"Relay Master", "Running"};

    test.tprintf("Checking before stopping IO thread");
    test.print_maxctrl("list servers");

    test.add_result(test.maxscale->get_server_status("server1") != master, "server1 is not a master");
    test.add_result(test.maxscale->get_server_status("server2") != slave, "server2 is not a slave");
    test.add_result(test.maxscale->get_server_status("server3") != relay_master,
                    "server3 is not a relay master");
    test.add_result(test.maxscale->get_server_status("server4") != slave, "server4 is not a slave");

    execute_query(test.repl->nodes[2], "STOP SLAVE IO_THREAD");
    sleep(10);

    test.tprintf("Checking after stopping IO thread");
    test.print_maxctrl("list servers");
    test.add_result(test.maxscale->get_server_status("server1") != master, "server1 is not a master");
    test.add_result(test.maxscale->get_server_status("server2") != slave, "server2 is not a slave");
    test.add_result(test.maxscale->get_server_status("server3") != running, "server3 is not only running");
    test.add_result(test.maxscale->get_server_status("server4") != running, "server4 is not only running");

    return test.global_result;
}
