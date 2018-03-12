/**
 * MXS-1678: Stopping IO thread on relay master causes it to be promoted as master
 *
 * https://jira.mariadb.org/browse/MXS-1678
 */
#include "testconnections.h"
#include <set>
#include <string>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.repl->connect();
    execute_query(test.repl->nodes[3], "STOP SLAVE");
    execute_query(test.repl->nodes[3], "CHANGE MASTER TO MASTER_HOST='%s', MASTER_PORT=%d",
                  test.repl->IP_private[2], test.repl->port[2]);
    execute_query(test.repl->nodes[3], "START SLAVE");
    sleep(5);

    StringSet master = {"Master", "Running"};
    StringSet slave = {"Slave", "Running"};
    StringSet relay_master = {"Relay Master", "Slave", "Running"};

    test.tprintf("Checking before stopping IO thread");
    int exit_code;
    char *output = test.maxscales->ssh_node_output(0, "maxadmin list servers", true, &exit_code);
    test.tprintf("%s", output);
    free(output);

    test.add_result(test.maxscales->get_server_status("server1") != master, "server1 is not a master");
    test.add_result(test.maxscales->get_server_status("server2") != slave, "server2 is not a slave");
    test.add_result(test.maxscales->get_server_status("server3") != relay_master, "server3 is not a relay master");
    test.add_result(test.maxscales->get_server_status("server4") != slave, "server4 is not a slave");

    execute_query(test.repl->nodes[2], "STOP SLAVE IO_THREAD");
    sleep(10);

    test.tprintf("Checking after stopping IO thread");
    output = test.maxscales->ssh_node_output(0, "maxadmin list servers", true, &exit_code);
    test.tprintf("%s", output);
    free(output);
    test.add_result(test.maxscales->get_server_status("server1") != master, "server1 is not a master");
    test.add_result(test.maxscales->get_server_status( "server2") != slave, "server2 is not a slave");
    test.add_result(test.maxscales->get_server_status("server3") != relay_master, "server3 is not a relay master");
    test.add_result(test.maxscales->get_server_status("server4") != slave, "server4 is not a slave");

    test.repl->fix_replication();
    return test.global_result;
}
