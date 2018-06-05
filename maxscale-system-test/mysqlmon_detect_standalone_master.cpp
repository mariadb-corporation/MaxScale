/**
 * @file mysqlmon_detect_standalone_master.cpp MySQL Monitor Standalone Master Test
 * - block all nodes, but one
 * - wait for monitor (monitor_interval), monitor should select remaining node as master
 * - check maxadmin output
 * - check that queries work
 * - unblock backend nodes
 * - wait for monitor
 * - check that monitor is still using the same node and that the old nodes are in maintenance mode
 */

#include <iostream>
#include <sstream>
#include "testconnections.h"
#include "fail_switch_rejoin_common.cpp"

using std::stringstream;
using std::cout;
using std::endl;

void check_maxscale(TestConnections& test)
{
    test.tprintf("Connecting to Maxscale\n");
    test.add_result(test.maxscales->connect_maxscale(0), "Can not connect to Maxscale\n");
    test.tprintf("Trying simple query against all sevices\n");
    test.tprintf("RWSplit \n");
    test.try_query(test.maxscales->conn_rwsplit[0], (char *) "show databases;");
    test.tprintf("ReadConn Master \n");
    test.try_query(test.maxscales->conn_master[0], (char *) "show databases;");
}

void replicate_from(TestConnections& test, int server_ind, int target_ind)
{
    stringstream change_master;
    change_master << "CHANGE MASTER TO MASTER_HOST = '" << test.repl->IP[target_ind]
        << "', MASTER_PORT = " << test.repl->port[target_ind] << ", MASTER_USE_GTID = current_pos, "
        "MASTER_USER='repl', MASTER_PASSWORD='repl';";
    cout << "Server " << server_ind + 1 << " starting to replicate from server " << target_ind + 1 << endl;
    if (test.verbose)
    {
       cout << "Query is '" << change_master.str() << "'" << endl;
    }
    test.try_query(test.repl->nodes[server_ind], "STOP SLAVE;");
    test.try_query(test.repl->nodes[server_ind], change_master.str().c_str());
    test.try_query(test.repl->nodes[server_ind], "START SLAVE;");
}

void restore_servers(TestConnections& test, bool events_added)
{
    test.repl->unblock_node(0);
    test.repl->unblock_node(1);
    test.repl->unblock_node(2);
    int dummy;
    char *o1 = test.maxscales->ssh_node_output(0, "maxadmin clear server server1 Maint", true, &dummy);
    char *o2 = test.maxscales->ssh_node_output(0, "maxadmin clear server server2 Maint", true, &dummy);
    char *o3 = test.maxscales->ssh_node_output(0, "maxadmin clear server server3 Maint", true, &dummy);
    free(o1);
    free(o2);
    free(o3);
    if (events_added)
    {
        // Events have been added to server4, so it must be the real new master. Then switchover to server1.
        replicate_from(test, 0, 3);
        replicate_from(test, 1, 3);
        replicate_from(test, 2, 3);
        sleep(10);
        o1 = test.maxscales->ssh_node_output(0,
            "maxadmin call command mariadbmon switchover MySQL-Monitor server1 server4", true, &dummy);
        sleep(10);
        int master_id = get_master_server_id(test);
        test.assert(master_id == 1, "Switchover failed to set server1 as master.");
    }
    else
    {
        // No events added, it should be enough to start slave on server4
        replicate_from(test, 3, 0);
    }
}

int main(int argc, char *argv[])
{
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);
    test.maxscales->connect_maxscale(0);
    test.repl->connect();
    delete_slave_binlogs(test);
    print_gtids(test);
    test.tprintf(" Create the test table and insert some data ");
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE OR REPLACE TABLE test.t1 (id int)");
    test.try_query(test.maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");
    test.repl->sync_slaves();

    print_gtids(test);

    test.maxscales->close_maxscale_connections(0);
    if (test.global_result != 0)
    {
        return test.global_result;
    }

    test.tprintf(" Block all but one node, stop slave on server 4 ");
    test.repl->block_node(0);
    test.repl->block_node(1);
    test.repl->block_node(2);

    test.try_query(test.repl->nodes[3], "STOP SLAVE;RESET SLAVE ALL;");

    test.tprintf(" Wait for the monitor to detect it ");
    sleep(10);

    test.tprintf(" Connect and insert should work ");
    get_output(test);

    int master_id = get_master_server_id(test);
    test.assert(master_id == 4, "Server 4 should be master, but master is server %d.", master_id);

    if (test.global_result != 0)
    {
       restore_servers(test, false);
       return test.global_result;
    }

    test.maxscales->connect_maxscale(0);
    test.try_query(test.maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");
    test.maxscales->close_maxscale_connections(0);
    test.repl->connect(3);
    char result_tmp[bufsize];
    if (find_field(test.repl->nodes[3], GTID_QUERY, GTID_FIELD, result_tmp) == 0)
    {
        test.tprintf("Node 3 gtid: %s", result_tmp);
    }

    test.tprintf("Unblock nodes ");
    test.repl->unblock_node(0);
    test.repl->unblock_node(1);
    test.repl->unblock_node(2);

    test.tprintf(" Wait for the monitor to detect it ");
    sleep(10);

    test.tprintf("Check that we are still using the last node to which we failed over "
                 "to and that the old nodes are in maintenance mode");

    test.maxscales->connect_maxscale(0);
    get_output(test);

    test.try_query(test.maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");
    master_id = get_master_server_id(test);
    test.tprintf("Master server id is %d", master_id);

    test.repl->connect();
    int real_id = test.repl->get_server_id(3);
    test.assert(master_id == real_id, "@@server_id is different: %d != %d", master_id, real_id);
    print_gtids(test);
    test.maxscales->close_maxscale_connections(0);

    test.tprintf("Check that MaxScale is running");
    check_maxscale(test);
    if (test.global_result == 0)
    {
       cout << "Test successful, restoring original state." << endl;
       restore_servers(test, true);
    }
    return test.global_result;
}
