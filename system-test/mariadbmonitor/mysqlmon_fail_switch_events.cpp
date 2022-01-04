/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include "failover_common.cpp"
#include <string>
#include <maxtest/envv.hh>

using std::string;

const char EVENT_NAME[] = "test_event";
const char EVENT_SHCEDULER[] = "SET GLOBAL event_scheduler = %s;";
const char USE_TEST[] = "USE test;";
const char DELETE_EVENT[] = "DROP EVENT %s;";

const char EV_STATE_ENABLED[] = "ENABLED";
const char EV_STATE_DISABLED[] = "DISABLED";
const char EV_STATE_SLAVE_DISABLED[] = "SLAVESIDE_DISABLED";

const char WRONG_MASTER_FMT[] = "%s is not master as expected. Current master id: %i.";

void expect_event_charset_collation(TestConnections& test, const string& event_name,
                                    const string& client_charset, const string& collation_connection,
                                    const string& database_collation);

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
            cout << "Event '" << event_name << "' is '" << status << "' on node " << node <<
                    " as it should.\n";
        }
    }
    return rval;
}

void set_event_state(TestConnections& test, const string& event_name, const string& new_state)
{
    bool success = false;
    test.maxscales->connect_maxscale(0);
    MYSQL* conn = test.maxscales->conn_rwsplit[0];
    const char query_fmt[] = "ALTER EVENT %s %s;";

    if ((test.try_query(conn, USE_TEST) == 0)
        && (test.try_query(conn, query_fmt, event_name.c_str(), new_state.c_str()) == 0))
    {
        success = true;
    }
    test.expect(success, "ALTER EVENT failed: %s", mysql_error(conn));
    if (success)
    {
        cout << "Event '" << event_name << "' set to '" << new_state << "'.\n";
    }
}

void switchover(TestConnections& test, const string& new_master)
{
    string switch_cmd = "call command mysqlmon switchover MySQL-Monitor " + new_master;
    test.maxscales->execute_maxadmin_command_print(0, switch_cmd.c_str());
    test.maxscales->wait_for_monitor(2);
    // Check success.
    auto new_master_status = test.get_server_status(new_master.c_str());
    auto new_master_id = test.get_master_server_id();
    string status_string;
    for (auto elem : new_master_status)
    {
        status_string += elem + ", ";
    }

    bool success = (new_master_status.count("Master") == 1);
    test.expect(success,
                "%s is not master as expected. Status: %s. Current master id: %i",
                new_master.c_str(), status_string.c_str(), new_master_id);

    if (success)
    {
        cout << "Switchover success, " + new_master + " is new master.\n";
    }
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

    int server1_ind = 0;
    int server2_ind = 1;
    int server1_id = test.repl->get_server_id(server1_ind);

    const int N = 4;
    const char* server_names[] = {"server1", "server2", "server3", "server4"};
    auto server1_name = server_names[server1_ind];
    auto server2_name = server_names[server2_ind];

    int master_id_begin = test.get_master_server_id();

    test.expect(master_id_begin == server1_id, WRONG_MASTER_FMT, server1_name, master_id_begin);

    // If initialisation failed, fail the test immediately.
    if (test.global_result != 0)
    {
        try_delete_event(test);
        return test.global_result;
    }

    // Part 1: Do a failover
    cout << "\nStep 1: Stop master and wait for failover. Check that another server is promoted.\n";
    test.repl->stop_node(server1_ind);
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
    cout << "\nStep 2: Restart " << server1_name << ". It should join the cluster.\n";
    test.repl->start_node(server1_ind);
    test.maxscales->wait_for_monitor(4);
    get_output(test);

    auto states = test.get_server_status(server1_name);
    if (states.count("Slave") < 1)
    {
        test.expect(false, "%s is not a slave as expected. Status: %s",
                    server1_name, string_set_to_string(states).c_str());
    }
    else
    {
        // Old master joined as slave, check that event is disabled.
        check_event_status(test, server1_ind, EVENT_NAME, EV_STATE_SLAVE_DISABLED);
    }

    if (test.global_result != 0)
    {
        try_delete_event(test);
        return test.global_result;
    }

    // Part 3: Switchover back to server1 as master. The event will most likely not run because the old
    // master doesn't have event scheduler on anymore.
    cout << "\nStep 3: Switchover back to " << server1_name << ". Check that event is enabled. "
            "Don't check that the event is running since the scheduler process is likely off.\n";
    switchover(test, server1_name);
    if (test.ok())
    {
        check_event_status(test, server1_ind, EVENT_NAME, EV_STATE_ENABLED);
    }

    // Part 4: Disable the event on master. The event should still be "SLAVESIDE_DISABLED" on slaves.
    // Check that after switchover, the event is not enabled.
    cout << "\nStep 4: Disable event on master, switchover to " << server2_name << ". "
            "Check that event is still disabled.\n";
    if (test.ok())
    {
        set_event_state(test, EVENT_NAME, "DISABLE");
        test.maxscales->wait_for_monitor(); // Wait for the monitor to detect the change.
        check_event_status(test, server1_ind, EVENT_NAME, EV_STATE_DISABLED);
        check_event_status(test, server2_ind, EVENT_NAME, EV_STATE_SLAVE_DISABLED);

        if (test.ok())
        {
            cout << "Event is disabled on master and slaveside-disabled on slave.\n";
            switchover(test, server2_name);
            if (test.ok())
            {
                // Event should not have been touched.
                check_event_status(test, server2_ind, EVENT_NAME, EV_STATE_SLAVE_DISABLED);
            }

            // Switchover back.
            switchover(test, server1_name);
        }
    }

    if (test.ok())
    {
        // Check that all other nodes are slaves.
        for (int i = 1; i < N; i++)
        {
            string server_name = server_names[i];
            states = test.maxscales->get_server_status(server_name.c_str());
            test.expect(states.count("Slave") == 1, "%s is not a slave.", server_name.c_str());
        }
    }

    if (test.ok())
    {
        // MXS-3158 Check that monitor preserves the character set and collation of an even when altering it.
        test.tprintf("Checking event handling with non-default charset and collation.");

        const char def_charset[] = "latin1";
        const char def_collation[] = "latin1_swedish_ci";

        expect_event_charset_collation(test, EVENT_NAME, def_charset, def_collation, def_collation);
        if (test.ok())
        {
            // Alter event charset to utf8.
            const char new_charset[] = "utf8mb4";
            const char new_collation[] = "utf8mb4_estonian_ci";
            auto conn = test.repl->nodes[0];
            test.try_query(conn, "SET NAMES %s COLLATE %s; ALTER EVENT %s ENABLE;",
                           new_charset, new_collation, EVENT_NAME);
            check_event_status(test, server1_ind, EVENT_NAME, EV_STATE_ENABLED);
            expect_event_charset_collation(test, EVENT_NAME, new_charset, new_collation, def_collation);

            if (test.ok())
            {
                switchover(test, server2_name);
                if (test.ok())
                {
                    check_event_status(test, server2_ind, EVENT_NAME, EV_STATE_ENABLED);
                    expect_event_charset_collation(test, EVENT_NAME, new_charset, new_collation,
                                                   def_collation);
                }

                // Switchover back.
                switchover(test, server1_name);
            }
        }
    }

    try_delete_event(test);
    if (test.global_result != 0)
    {
        test.repl->fix_replication();
    }
    return test.global_result;
}

void expect_event_charset_collation(TestConnections& test, const string& event_name,
                                    const string& client_charset, const string& collation_connection,
                                    const string& database_collation)
{
    auto conn = test.maxscales->rwsplit();
    conn.connect();
    string query = string_printf("select CHARACTER_SET_CLIENT, COLLATION_CONNECTION, DATABASE_COLLATION "
                                 "from information_schema.EVENTS where EVENT_NAME = '%s';",
                                 event_name.c_str());
    Row row = conn.row(query);
    if (!row.empty())
    {
        string& found_charset = row[0];
        string& found_collation = row[1];
        string& found_dbcoll = row[2];

        test.tprintf("Event '%s': CHARACTER_SET_CLIENT is '%s', COLLATION_CONNECTION is '%s', "
                     "DATABASE_COLLATION is '%s'",
                     EVENT_NAME, found_charset.c_str(), found_collation.c_str(), found_dbcoll.c_str());
        const char error_fmt[] = "Wrong %s. Found %s, expected %s.";
        test.expect(found_charset == client_charset, error_fmt, "CHARACTER_SET_CLIENT",
                    found_charset.c_str(), client_charset.c_str());
        test.expect(found_collation == collation_connection, error_fmt, "COLLATION_CONNECTION",
                    found_collation.c_str(), collation_connection.c_str());
        test.expect(found_dbcoll == database_collation, error_fmt, "DATABASE_COLLATION",
                    found_dbcoll.c_str(), database_collation.c_str());
    }
    else
    {
        test.expect(false, "Query '%s' failed.", query.c_str());
    }
}
