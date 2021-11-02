/**
 * MXS-2146: Test case for csmon
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections::require_columnstore(true);
    TestConnections test(argc, argv);

    // Simple check for correct routing behavior
    test.maxscale->connect();
    auto slave = get_row(test.maxscale->conn_rwsplit, "SELECT @@server_id");
    test.try_query(test.maxscale->conn_rwsplit, "BEGIN");
    auto master = get_row(test.maxscale->conn_rwsplit, "SELECT @@server_id");
    test.try_query(test.maxscale->conn_rwsplit, "COMMIT");
    test.expect(slave != master, "Master and slave server_id should be different");
    test.maxscale->disconnect();

    // Master failures are detected
    test.maxscale->connect();
    test.repl->block_node(0);
    test.expect(execute_query_silent(test.maxscale->conn_rwsplit, "SELECT @@last_insert_id") != 0,
                "Query should fail when the master is blocked");
    test.repl->unblock_node(0);
    test.maxscale->disconnect();

    // Slave failures are detected
    test.maxscale->connect();
    test.repl->block_node(1);
    test.maxscale->wait_for_monitor();
    auto backup_slave = get_row(test.maxscale->conn_rwsplit, "SELECT @@server_id");
    test.expect(backup_slave == master, "Query should go to the master when the slave is down");
    test.repl->unblock_node(1);
    test.maxscale->disconnect();

    return test.global_result;
}
