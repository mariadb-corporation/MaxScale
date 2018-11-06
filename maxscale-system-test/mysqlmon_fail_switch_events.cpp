/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "testconnections.h"
#include "failover_common.cpp"
#include <string>

using std::string;

const char EVENT_NAME[] = "test_event";
const char EVENT_SHCEDULER[] = "SET GLOBAL event_scheduler = %s;";
const char USE_TEST[] = "USE test;";
const char DELETE_EVENT[] = "DROP EVENT %s;";

int read_incremented_field(TestConnections& test)
{
    int rval = -1;
    MYSQL* conn = test.maxscales->open_rwsplit_connection();
    char output[100];
    if (find_field(conn, "SELECT * FROM test.t1;", "c1", output) == 0)
    {
        char* endptr = NULL;
        auto colvalue = strtol(output, &endptr, 0);
        if (endptr && *endptr == '\0')
        {
            rval = colvalue;
        }
        else
        {
            test.expect(false, "Could not read value from query result '%s'.", output);
        }
    }
    else
    {
        test.expect(false, "Could not perform query: %s.", mysql_error(conn));
    }
    return rval;
}

bool field_is_incrementing(TestConnections& test)
{
    int old_val = read_incremented_field(test);
    sleep(2);   // Should be enough to allow the event to run once.
    // Check that the event is running and increasing the value
    int new_val = read_incremented_field(test);
    return new_val > old_val;
}

void create_event(TestConnections& test)
{
    // Create table, enable scheduler and add an event
    test.tprintf("Creating table, inserting data and scheduling an event.");
    test.maxscales->connect_maxscale(0);
    MYSQL* conn = test.maxscales->conn_rwsplit[0];
    const char create_event_query[] = "CREATE EVENT %s ON SCHEDULE EVERY 1 SECOND "
                                      "DO UPDATE test.t1 SET c1 = c1 + 1;";

    if ((test.try_query(conn, EVENT_SHCEDULER, "ON") == 0)
        && (test.try_query(conn, "CREATE OR REPLACE TABLE test.t1(c1 INT);") == 0)
        && (test.try_query(conn, USE_TEST) == 0)
        && (test.try_query(conn, "INSERT INTO t1 VALUES (1);") == 0)
        && (test.try_query(conn, create_event_query, EVENT_NAME) == 0))
    {
        test.repl->sync_slaves();
        // Check that the event is running and increasing the value
        test.expect(field_is_incrementing(test),
                    "Value in column did not increment. Current value %i.", read_incremented_field(test));
    }
    print_gtids(test);
}

void delete_event(TestConnections& test)
{
    test.maxscales->connect_maxscale(0);
    MYSQL* conn = test.maxscales->conn_rwsplit[0];

    if ((test.try_query(conn, EVENT_SHCEDULER, "OFF") == 0)
        && (test.try_query(conn, USE_TEST) == 0)
        && (test.try_query(conn, DELETE_EVENT, EVENT_NAME) == 0))
    {
        test.repl->sync_slaves();
        test.expect(!field_is_incrementing(test),
                    "Value in column was incremented when it should not be. Current value %i.",
                    read_incremented_field(test));
    }
}

void try_delete_event(TestConnections& test)
{
    test.maxscales->connect_maxscale(0);
    MYSQL* conn = test.maxscales->conn_rwsplit[0];

    execute_query(conn, EVENT_SHCEDULER, "OFF");
    execute_query(conn, USE_TEST);
    execute_query(conn, DELETE_EVENT, EVENT_NAME);
    test.repl->sync_slaves();
}

string string_set_to_string(const StringSet& set)
{
    string rval;
    for (auto elem : set)
    {
        rval += elem + ", ";
    }
    return rval;
}

bool check_event_status(TestConnections& test, int node,
                        const string& event_name, const string& expected_state)
{
    bool rval = false;
    test.repl->connect();
    string query = "SELECT * FROM information_schema.EVENTS WHERE EVENT_NAME = '" + event_name + "';";
    char status[100];
    if (find_field(test.repl->nodes[node], query.c_str(), "STATUS", status) != 0)
    {
        test.expect(false, "Could not query event status: %s", mysql_error(test.repl->nodes[0]));
    }
    else
    {
        if (expected_state != status)
        {
            test.expect(false, "Wrong event status, found %s when %s was expected.",
                        status, expected_state.c_str());
        }
        else
        {
            rval = true;
            cout << "Event '" << event_name << "' is '" << status << "' as it should.\n";
        }
    }
    return rval;
}

int main(int argc, char** argv)
{
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);
    test.repl->connect();
    delete_slave_binlogs(test);

    try_delete_event(test);
    // Schedule a repeating event.
    create_event(test);

    int master_id_begin = test.get_master_server_id();
    int node0_id = test.repl->get_server_id(0);
    test.expect(master_id_begin == node0_id,
                "First server is not the master: master id: %i", master_id_begin);

    // If initialisation failed, fail the test immediately.
    if (test.global_result != 0)
    {
        try_delete_event(test);
        return test.global_result;
    }

    // Part 1: Do a failover
    cout << "Step 1: Stop master and wait for failover. Check that another server is promoted.\n";
    test.repl->stop_node(0);
    test.maxscales->wait_for_monitor(3);
    get_output(test);
    int master_id_failover = test.get_master_server_id();
    cout << "Master server id is " << master_id_failover << ".\n";
    test.expect(master_id_failover > 0 && master_id_failover != master_id_begin,
                "Master did not change or no master detected.");
    // Check that events are still running.
    test.expect(field_is_incrementing(test),
                "Value in column did not increment. Current value %i.",
                read_incremented_field(test));
    // Again, stop on failure.
    if (test.global_result != 0)
    {
        try_delete_event(test);
        return test.global_result;
    }

    // Part 2: Start node 0, let it join the cluster and check that the event is properly disabled.
    cout << "Step 2: Restart node 0. It should join the cluster.\n";
    test.repl->start_node(0);
    test.maxscales->wait_for_monitor(4);
    get_output(test);
    const char server_name[] = "server1";
    auto states = test.get_server_status(server_name);
    if (states.count("Slave") < 1)
    {
        test.expect(false, "%s is not a slave as expected. Status: %s",
                    server_name, string_set_to_string(states).c_str());
    }
    else
    {
        // Old master joined as slave, check that event is disabled.
        check_event_status(test, 0, EVENT_NAME, "SLAVESIDE_DISABLED");
    }

    if (test.global_result != 0)
    {
        try_delete_event(test);
        return test.global_result;
    }

    // Part 3: Switchover back to server1 as master. The event will most likely not run because the old
    // master doesn't have event scheduler on anymore.
    cout << "Step 3: Switchover back to server1. Check that event is enabled. Don't check that the "
            "event is running since the scheduler process is likely off.\n";
    string switch_cmd = "call command mysqlmon switchover MySQL-Monitor server1";
    test.maxscales->execute_maxadmin_command_print(0, switch_cmd.c_str());
    test.maxscales->wait_for_monitor(1);
    get_output(test);
    // Check success.
    int master_id_switchover = test.get_master_server_id();
    test.expect(master_id_switchover == node0_id,
                "server1 is not master as expected. Current master: %i.", master_id_switchover);
    check_event_status(test, 0, EVENT_NAME, "ENABLED");
    if (test.global_result != 0)
    {
        try_delete_event(test);
        return test.global_result;
    }

    // Check that all other nodes are slaves.
    for (int i = 1; i < test.repl->N; i++)
    {
        string server_name = "server" + std::to_string(i + 1);
        auto states = test.maxscales->get_server_status(server_name.c_str());
        test.expect(states.count("Slave") == 1, "%s is not a slave.", server_name.c_str());
    }

    try_delete_event(test);
    if (test.global_result != 0)
    {
        test.repl->fix_replication();
    }
    return test.global_result;
}
