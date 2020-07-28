/**
 * MXS-2146: Test case for csmon
 */

#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections::require_columnstore(true);
    TestConnections test(argc, argv);

    // Simple check for correct routing behavior
    test.maxscales->connect();
    auto slave = get_row(test.maxscales->conn_rwsplit[0], "SELECT @@server_id");
    test.try_query(test.maxscales->conn_rwsplit[0], "BEGIN");
    auto master = get_row(test.maxscales->conn_rwsplit[0], "SELECT @@server_id");
    test.try_query(test.maxscales->conn_rwsplit[0], "COMMIT");
    test.expect(slave != master, "Master and slave server_id should be different");
    test.maxscales->disconnect();

    // Master failures are detected
    test.maxscales->connect();
    test.repl->block_node(0);
    test.expect(execute_query_silent(test.maxscales->conn_rwsplit[0], "SELECT @@last_insert_id") != 0,
                "Query should fail when the master is blocked");
    test.repl->unblock_node(0);
    test.maxscales->disconnect();

    // Slave failures are detected
    test.maxscales->connect();
    test.repl->block_node(1);
    test.maxscales->wait_for_monitor();
    auto backup_slave = get_row(test.maxscales->conn_rwsplit[0], "SELECT @@server_id");
    test.expect(backup_slave == master, "Query should go to the master when the slave is down");
    test.repl->unblock_node(1);
    test.maxscales->disconnect();

    return test.global_result;
}
