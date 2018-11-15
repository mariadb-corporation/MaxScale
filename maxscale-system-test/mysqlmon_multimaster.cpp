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
 *
 * Addition: add delays to some slave connections and check that the monitor correctly detects the delay
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"
#include <jansson.h>
#include <string>

using std::cout;
using std::string;

void check_status(TestConnections& test, const char* server, const char* status)
{
    char cmd[256];
    char maxadmin_result[1024];

    sprintf(cmd, "show server %s", server);
    test.maxscales->get_maxadmin_param(0, cmd, (char*)"Status:", maxadmin_result);

    if (maxadmin_result == NULL)
    {
        test.add_result(1, "maxadmin execution error\n");
        return;
    }

    if (strstr(maxadmin_result, status) == NULL)
    {
        test.add_result(1, "Test failed, server '%s' status is '%s', expected '%s'\n",
                        server, maxadmin_result, status);
    }
}

json_t* get_json_data(TestConnections& test, const char* query)
{
    json_t* rval = NULL;
    int exit_code = 1;
    char* output = test.maxscales->ssh_node_output(0, query, true, &exit_code);
    if (output == NULL)
    {
        test.add_result(1, "Query '%s' execution error, no output.\ni", output);
    }
    else
    {
        json_error_t error;
        rval = json_loads(output, 0, &error);
        free(output);
        if (rval == NULL)
        {
            test.add_result(1, "JSON decode error: %s\n", error.text);
        }
    }
    return rval;
}

json_t* traverse_json(TestConnections& test, json_t* object, const std::vector<string>& keys)
{
    test.expect(object, "JSON object is NULL\n");

    json_t* current_object = object;
    for (auto iter = keys.begin(); iter != keys.end() && current_object; ++iter)
    {
        current_object = json_object_get(current_object, (*iter).c_str());
        test.expect(current_object, "Key %s was not found in json data.\n", (*iter).c_str());
    }
    return current_object;
}

json_t* find_array_elem_json(TestConnections& test, json_t* object,
                             std::string key, std::string expected_val)
{
    bool is_array = json_is_array(object);
    test.expect(is_array, "JSON object is not an array\n");
    json_t* found_elem = NULL;

    if (is_array)
    {
        size_t arr_size = json_array_size(object);
        for (size_t i = 0; i < arr_size; i++)
        {
            json_t* arr_elem = json_array_get(object, i);
            json_t* elem_val = json_object_get(arr_elem, key.c_str());
            bool is_string = json_is_string(elem_val);
            test.expect(is_string, "Key %s was not found in json data or the data is not string.\n", key.c_str());
            if (is_string)
            {
                std::string elem_field = json_string_value(elem_val);
                if (elem_field == expected_val)
                {
                    found_elem = arr_elem;
                    break;
                }
            }
        }

        test.expect(found_elem, "Array element with %s->%s was not found in json array\n",
                    key.c_str(), expected_val.c_str());
    }
    return found_elem;
}

void check_group(TestConnections& test, const char* server, int expected_group)
{
    json_t* monitor_data = get_json_data(test, "maxctrl api get monitors/MySQL-Monitor");
    if (monitor_data == NULL)
    {
        return;
    }

    monitor_data = traverse_json(test, monitor_data,
                                 {"data", "attributes", "monitor_diagnostics", "server_info"});
    if (monitor_data)
    {
        auto server_data = find_array_elem_json(test, monitor_data, "name", server);
        if (server_data)
        {
            int found_group = json_integer_value(json_object_get(server_data, "master_group"));
            test.expect(found_group == expected_group, "Server '%s', expected group '%d', not '%d'",
                        server, expected_group, found_group);
        }
    }
}

void check_rlag(TestConnections& test, const char* server, int min_rlag, int max_rlag)
{
    json_t* object_data = get_json_data(test, "maxctrl api get servers");
    if (object_data)
    {
        auto servers_data = traverse_json(test, object_data, {"data"});
        auto server_data = find_array_elem_json(test, servers_data, "id", server);
        auto rlag_object = traverse_json(test, server_data, {"attributes", "replication_lag"});
        if (rlag_object)
        {
            int found_rlag = json_integer_value(rlag_object);
            if (found_rlag >= min_rlag && found_rlag <= max_rlag)
            {
                cout << "Replication lag of " << server << " is " << found_rlag << " seconds.\n";
            }
            else
            {
                test.expect(false,
                            "Replication lag of %s is out of bounds: value: %i min: %i max: %i\n",
                            server, found_rlag, min_rlag, max_rlag);
            }
        }
    }
}

void change_master(TestConnections& test ,int slave, int master, const string& conn_name = "",
                   int replication_delay = 0)
{
    const char query[] = "CHANGE MASTER '%s' TO master_host='%s', master_port=%d, "
                         "master_log_file='mar-bin.000001', master_log_pos=4, "
                         "master_user='repl', master_password='repl', master_delay=%d; "
                         "START SLAVE '%s';";
    test.try_query(test.repl->nodes[slave], query, conn_name.c_str(),
		   test.repl->IP[master], test.repl->port[master],
                   replication_delay, conn_name.c_str());
}

int main(int argc, char* argv[])
{
    const char mm_master_states[] = "Master, Running";
    const char mm_slave_states[] = "Relay Master, Slave, Running";
    const char slave_states[] = "Slave, Running";
    const char running_state[] = "Running";
    const char reset_query[] = "STOP SLAVE; RESET SLAVE ALL; RESET MASTER; SET GLOBAL read_only='OFF'";
    const char readonly_on_query[] = "SET GLOBAL read_only='ON'";

    TestConnections::require_repl_version("10.2.3"); // Delayed replication needs this.
    TestConnections test(argc, argv);

    test.tprintf("Test 1 - Configure all servers into a multi-master ring with one slave");
    int max_rlag = 100;
    test.set_timeout(120);
    test.repl->execute_query_all_nodes(reset_query);
    test.repl->connect();
    change_master(test, 0, 1);
    change_master(test, 1, 2);
    change_master(test, 2, 0);
    change_master(test, 3, 2, "", max_rlag);

    test.maxscales->wait_for_monitor(2);
    auto maxconn = test.maxscales->open_rwsplit_connection();
    test.try_query(maxconn, "FLUSH TABLES;");
    mysql_close(maxconn);

    test.maxscales->wait_for_monitor(1);

    check_status(test, "server1", mm_master_states);
    check_status(test, "server2", mm_slave_states);
    check_status(test, "server3", mm_slave_states);
    check_status(test, "server4", slave_states);
    check_group(test, "server1", 1);
    check_group(test, "server2", 1);
    check_group(test, "server3", 1);
    check_group(test, "server4", 0);
    check_rlag(test, "server4", 1, max_rlag);

    test.tprintf("Test 2 - Set nodes 0 and 1 into read-only mode");

    test.set_timeout(120);
    execute_query(test.repl->nodes[0], readonly_on_query);
    execute_query(test.repl->nodes[1], readonly_on_query);
    test.maxscales->wait_for_monitor(1);

    check_status(test, "server1", mm_slave_states);
    check_status(test, "server2", mm_slave_states);
    check_status(test, "server3", mm_master_states);
    check_status(test, "server4", slave_states);
    check_group(test, "server1", 1);
    check_group(test, "server2", 1);
    check_group(test, "server3", 1);
    check_group(test, "server4", 0);
    check_rlag(test, "server4", 1, max_rlag);

    test.tprintf("Test 3 - Configure nodes 1 and 2 into a master-master pair, make node 0 "
                 "a slave of node 1 and node 3 a slave of node 2");

    test.set_timeout(120);
    test.repl->execute_query_all_nodes(reset_query);
    test.repl->connect();
    change_master(test, 0, 1);
    change_master(test, 1, 2);
    change_master(test, 2, 1, "", max_rlag);
    change_master(test, 3, 2);

    test.maxscales->wait_for_monitor(1);
    maxconn = test.maxscales->open_rwsplit_connection();
    test.try_query(maxconn, "FLUSH TABLES;");
    mysql_close(maxconn);
    test.maxscales->wait_for_monitor(1);

    check_status(test, "server1", slave_states);
    check_status(test, "server2", mm_master_states);
    check_status(test, "server3", mm_slave_states);
    check_status(test, "server4", slave_states);
    check_group(test, "server1", 0);
    check_group(test, "server2", 1);
    check_group(test, "server3", 1);
    check_group(test, "server4", 0);
    check_rlag(test, "server3", 1, max_rlag);

    test.tprintf("Test 4 - Set node 1 into read-only mode");

    test.set_timeout(120);
    execute_query(test.repl->nodes[1], readonly_on_query);
    test.maxscales->wait_for_monitor(1);

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

    test.maxscales->wait_for_monitor(1);

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

    test.maxscales->wait_for_monitor(1);

    check_status(test, "server1", mm_master_states);
    check_status(test, "server2", mm_slave_states);
    check_status(test, "server3", running_state);
    check_status(test, "server4", running_state);
    check_group(test, "server1", 1);
    check_group(test, "server2", 1);
    check_group(test, "server3", 2);
    check_group(test, "server4", 2);

    test.tprintf("Test 7 - Diamond topology with delay");

    test.repl->execute_query_all_nodes(reset_query);
    test.repl->connect();
    change_master(test, 0, 1, "a", max_rlag);
    change_master(test, 0, 2, "b", max_rlag);
    change_master(test, 1, 3);
    change_master(test, 2, 3);

    test.maxscales->wait_for_monitor(1);
    maxconn = test.maxscales->open_rwsplit_connection();
    test.try_query(maxconn, "FLUSH TABLES;");
    mysql_close(maxconn);
    test.maxscales->wait_for_monitor(1);

    check_status(test, "server1", slave_states);
    check_status(test, "server2", mm_slave_states);
    check_status(test, "server3", mm_slave_states);
    check_status(test, "server4", mm_master_states);
    check_group(test, "server1", 0);
    check_group(test, "server2", 0);
    check_group(test, "server3", 0);
    check_group(test, "server4", 0);
    check_rlag(test, "server1", 1, max_rlag);

    test.tprintf("Test 8 - Diamond topology with no delay");

    const char remove_delay[] = "STOP SLAVE '%s'; CHANGE MASTER '%s' TO master_delay=0; START SLAVE '%s';";
    test.try_query(test.repl->nodes[0], remove_delay, "a", "a", "a");
    test.maxscales->wait_for_monitor(1);

    check_status(test, "server1", slave_states);
    check_rlag(test, "server1", 0, 0);

    // Test over, reset topology.
    const char reset_with_name[] = "STOP SLAVE '%s'; RESET SLAVE '%s' ALL;";
    test.try_query(test.repl->nodes[0], reset_with_name, "a", "a");
    test.try_query(test.repl->nodes[0], reset_with_name, "b", "b");

    test.repl->execute_query_all_nodes(reset_query);
    test.repl->connect();
    change_master(test, 1, 0);
    change_master(test, 2, 0);
    change_master(test, 3, 0);

    return test.global_result;
}
