/**
 * @file mysqlmon_multimaster.cpp MySQL Monitor Multi-master Test
 * - Configure all servers into a multi-master ring with one slave
 * - check status using Maxadmin 'show servers' and 'show monitor "MySQL Monitor"'
 * - Set nodes 0 and 1 into read-only mode
 * - repeat status check
 * - Configure nodes 1 and 2 (server2 and server3) into a master-master pair, make node 0 a slave of node 1
 * and node 3 a slave of node 2
 * - repeat status check
 * - Set node 1 into read-only mode
 * - repeat status check
 * - Create two distinct groups (server1 and server2 are masters for eache others and same for server3 and
 * server4)
 * - repeat status check
 * - Set nodes 1 and 3 (server2 and server4) into read-only mode
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"
#include <jansson.h>

void check_status(TestConnections& test, const char* server, const char* status)
{
    char cmd[256];
    char maxadmin_result[1024];

    sprintf(cmd, "show server %s", server);
    test.set_timeout(120);
    test.maxscales->get_maxadmin_param(0, cmd, (char*)"Status:", maxadmin_result);

    if (maxadmin_result == NULL)
    {
        test.add_result(1, "maxadmin execution error\n");
        return;
    }

    if (strstr(maxadmin_result, status) == NULL)
    {
        test.add_result(1,
                        "Test failed, server '%s' status is '%s', expected '%s'\n",
                        server,
                        maxadmin_result,
                        status);
    }
}

void check_group(TestConnections& test, const char* server, int expected_group)
{
    int exit_code = 1;
    char* output = test.maxscales->ssh_node_output(0,
                                                   "maxctrl api get monitors/MySQL-Monitor",
                                                   true,
                                                   &exit_code);
    if (output == NULL)
    {
        test.add_result(1, "maxctrl execution error, no output\n");
        return;
    }

    json_error_t error;
    json_t* object_data = json_loads(output, 0, &error);
    if (object_data == NULL)
    {
        test.add_result(1, "JSON decode error: %s\n", error.text);
        return;
    }

    object_data = json_object_get(object_data, "data");
    object_data = json_object_get(object_data, "attributes");
    object_data = json_object_get(object_data, "monitor_diagnostics");
    json_t* server_info = json_object_get(object_data, "server_info");
    size_t arr_size = json_array_size(server_info);
    json_int_t found_group = -1;
    for (size_t i = 0; i < arr_size; i++)
    {
        json_t* arr_elem = json_array_get(server_info, i);
        std::string server_name = json_string_value(json_object_get(arr_elem, "name"));
        if (server_name == server)
        {
            found_group = json_integer_value(json_object_get(arr_elem, "master_group"));
        }
    }

    test.expect(found_group == expected_group,
                "Server '%s', expected group '%d', not '%d'",
                server,
                expected_group,
                (int)found_group);
}

void change_master(TestConnections& Test, int slave, int master)
{
    const char query[] = "CHANGE MASTER TO master_host='%s', master_port=%d, "
                         "master_log_file='mar-bin.000001', master_log_pos=4, master_user='repl', master_password='repl'; "
                         "START SLAVE";
    execute_query(Test.repl->nodes[slave], query, Test.repl->IP[master], Test.repl->port[master]);
}

int main(int argc, char* argv[])
{
    const char mm_master_states[] = "Master, Running";
    const char mm_slave_states[] = "Relay Master, Slave, Running";
    const char slave_states[] = "Slave, Running";
    const char running_state[] = "Running";
    const char reset_query[] = "STOP SLAVE; RESET SLAVE ALL; RESET MASTER; SET GLOBAL read_only='OFF'";
    const char readonly_on_query[] = "SET GLOBAL read_only='ON'";

    TestConnections test(argc, argv);

    test.tprintf("Test 1 - Configure all servers into a multi-master ring with one slave");

    test.set_timeout(120);
    test.repl->execute_query_all_nodes(reset_query);
    test.repl->connect();
    change_master(test, 0, 1);
    change_master(test, 1, 2);
    change_master(test, 2, 0);
    change_master(test, 3, 2);

    sleep(2);

    check_status(test, "server1", mm_master_states);
    check_status(test, "server2", mm_slave_states);
    check_status(test, "server3", mm_slave_states);
    check_status(test, "server4", slave_states);
    check_group(test, "server1", 1);
    check_group(test, "server2", 1);
    check_group(test, "server3", 1);
    check_group(test, "server4", 0);

    test.tprintf("Test 2 - Set nodes 0 and 1 into read-only mode");

    test.set_timeout(120);
    execute_query(test.repl->nodes[0], readonly_on_query);
    execute_query(test.repl->nodes[1], readonly_on_query);

    sleep(2);

    check_status(test, "server1", mm_slave_states);
    check_status(test, "server2", mm_slave_states);
    check_status(test, "server3", mm_master_states);
    check_status(test, "server4", slave_states);
    check_group(test, "server1", 1);
    check_group(test, "server2", 1);
    check_group(test, "server3", 1);
    check_group(test, "server4", 0);

    test.tprintf("Test 3 - Configure nodes 1 and 2 into a master-master pair, make node 0 "
                 "a slave of node 1 and node 3 a slave of node 2");

    test.set_timeout(120);
    test.repl->execute_query_all_nodes(reset_query);
    test.repl->connect();
    change_master(test, 0, 1);
    change_master(test, 1, 2);
    change_master(test, 2, 1);
    change_master(test, 3, 2);

    sleep(2);

    check_status(test, "server1", slave_states);
    check_status(test, "server2", mm_master_states);
    check_status(test, "server3", mm_slave_states);
    check_status(test, "server4", slave_states);
    check_group(test, "server1", 0);
    check_group(test, "server2", 1);
    check_group(test, "server3", 1);
    check_group(test, "server4", 0);

    test.tprintf("Test 4 - Set node 1 into read-only mode");

    test.set_timeout(120);
    execute_query(test.repl->nodes[1], readonly_on_query);

    sleep(2);

    check_status(test, "server1", slave_states);
    check_status(test, "server2", mm_slave_states);
    check_status(test, "server3", mm_master_states);
    check_status(test, "server4", slave_states);
    check_group(test, "server1", 0);
    check_group(test, "server2", 1);
    check_group(test, "server3", 1);
    check_group(test, "server4", 0);

    test.tprintf("Test 5 - Create two distinct groups");

    test.set_timeout(120);
    test.repl->execute_query_all_nodes(reset_query);
    test.repl->connect();
    change_master(test, 0, 1);
    change_master(test, 1, 0);
    change_master(test, 2, 3);
    change_master(test, 3, 2);

    sleep(2);

    // Even though the servers are in two distinct groups, only one of them
    // contains a master and a slave. Only one master may exist in a cluster
    // at once, since by definition this is the server to which routers may
    // direct writes.
    check_status(test, "server1", mm_master_states);
    check_status(test, "server2", mm_slave_states);
    check_status(test, "server3", running_state);
    check_status(test, "server4", running_state);
    check_group(test, "server1", 1);
    check_group(test, "server2", 1);
    check_group(test, "server3", 2);
    check_group(test, "server4", 2);

    test.tprintf("Test 6 - Set nodes 1 and 3 into read-only mode");

    test.set_timeout(120);
    execute_query(test.repl->nodes[1], readonly_on_query);
    execute_query(test.repl->nodes[3], readonly_on_query);

    sleep(2);

    check_status(test, "server1", mm_master_states);
    check_status(test, "server2", mm_slave_states);
    check_status(test, "server3", running_state);
    check_status(test, "server4", running_state);
    check_group(test, "server1", 1);
    check_group(test, "server2", 1);
    check_group(test, "server3", 2);
    check_group(test, "server4", 2);

    test.repl->execute_query_all_nodes(reset_query);
    test.repl->connect();
    change_master(test, 1, 0);
    change_master(test, 2, 0);
    change_master(test, 3, 0);
    test.repl->fix_replication();

    return test.global_result;
}
